#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor to instantiate/scale/monitor services like firewall, LB, ...
"""

import gevent
from gevent import ssl, monkey; monkey.patch_all()
import sys
import signal
import cStringIO
import requests
import ConfigParser
import cgitb
import logging
import logging.handlers
import copy
import argparse
import socket
import time
import datetime

import StringIO
import re

import pycassa
from pycassa.system_manager import *

import cfgm_common
from cfgm_common.imid import * 
from cfgm_common import vnc_cpu_info

from vnc_api.vnc_api import *

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.service_instance.ttypes import *
from cfgm_common.sandesh.vns.ttypes import Module
from cfgm_common.sandesh.vns.constants import ModuleNames
from sandesh.svc_mon_introspect import ttypes as sandesh

#nova imports
from novaclient import client as nc
from novaclient import exceptions as nc_exc

import discovery.client as client

_SVC_VN_MGMT = "svc-vn-mgmt"
_SVC_VN_LEFT = "svc-vn-left"
_SVC_VN_RIGHT = "svc-vn-right"
_MGMT_STR = "management"
_LEFT_STR = "left"
_RIGHT_STR = "right"

_SVC_VNS = {_MGMT_STR:  [_SVC_VN_MGMT,  '250.250.1.0/24'],
            _LEFT_STR:  [_SVC_VN_LEFT,  '250.250.2.0/24'],
            _RIGHT_STR: [_SVC_VN_RIGHT, '250.250.3.0/24']}

_CHECK_SVC_VM_HEALTH_INTERVAL = 30
_CHECK_CLEANUP_INTERVAL = 5 
_ERROR_LOG_FILE_SIZE = 64*1024

class SvcMonitor(object):
    """
    data + methods used/referred to by ssrc and arc greenlets
    """
    
    _KEYSPACE = 'svc_monitor_keyspace'
    _SVC_VM_CF = 'svc_vm_table'
    _CLEANUP_CF = 'cleanup_table'
    def __init__(self, vnc_lib, args=None):
        self._args = args

        #api server and cassandra init
        self._vnc_lib = vnc_lib
        self._cassandra_init()

        #dictionary for nova
        self._nova = {}

        #initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc= client.DiscoveryClient(self._args.disc_server_ip, 
                                               self._args.disc_server_port,
                                               ModuleNames[Module.SVC_MONITOR])

        #sandesh init
        collectors = None
        if self._args.collector and self._args.collector_port:
            collectors = [(self._args.collector, int(self._args.collector_port))]
        self._sandesh = Sandesh()
        sandesh.ServiceInstanceList.handle_request = self.sandesh_si_handle_request
        self._sandesh.init_generator(ModuleNames[Module.SVC_MONITOR], 
                socket.gethostname(), collectors, 'svc_monitor_context', 
                int(self._args.http_server_port), 
                ['cfgm_common', 'sandesh'], self._disc)
        self._sandesh.set_logging_params(enable_local_log = self._args.log_local,
                                         category = self._args.log_category,
                                         level = self._args.log_level,
                                         file = self._args.log_file)

        #create default analyzer template
        self._create_default_template('analyzer-template', 'analyzer', 'analyzer')
        self._create_default_template('nat-template', 'nat-service', 'firewall', 'in-network-nat')

        #create cpu_info object to send periodic updates
        sysinfo_req = False
        cpu_info = vnc_cpu_info.CpuInfo(Module.SVC_MONITOR, sysinfo_req, self._sandesh, 60)
        self._cpu_info = cpu_info

        # logging
        self._err_file = '/var/log/contrail/svc-monitor.err'
        self._tmp_file = '/var/log/contrail/svc-monitor.tmp'
        self._svc_err_logger = logging.getLogger('SvcErrLogger')
        self._svc_err_logger.setLevel(logging.ERROR)
        handler = logging.handlers.RotatingFileHandler(self._err_file,
                                                       maxBytes=_ERROR_LOG_FILE_SIZE,
                                                       backupCount=2)
        self._svc_err_logger.addHandler(handler)
    #end __init__

    #create service template
    def _create_default_template(self, st_name, image_name, svc_type, svc_mode=None):
        domain_name = 'default-domain'
        domain_fq_name = [domain_name]
        st_fq_name = [domain_name, st_name]
        self._svc_syslog("Creating %s %s image %s" % (domain_name, st_name, image_name))

        try:
            st_obj = self._vnc_lib.service_template_read(fq_name=st_fq_name)
            st_uuid = st_obj.uuid
        except NoIdError:
            domain = self._vnc_lib.domain_read(fq_name=domain_fq_name)
            st_obj = ServiceTemplate(name=st_name, domain_obj=domain)
            st_uuid = self._vnc_lib.service_template_create(st_obj)

        svc_properties = ServiceTemplateType()
        svc_properties.set_image_name(image_name)
        svc_properties.set_service_type(svc_type)

        #set interface list
        if svc_type == 'analyzer':
            if_list = [['left', False]]
        else:
            if_list = [['left', False], ['right', False], ['management', False]]
            svc_properties.set_service_mode(svc_mode)

        for itf in if_list:
            if_type = ServiceTemplateInterfaceType(shared_ip=itf[1])
            if_type.set_service_interface_type(itf[0])
            svc_properties.add_interface_type(if_type)

        try:
            st_obj.set_service_template_properties(svc_properties)
            self._vnc_lib.service_template_update(st_obj)
        except Exception as e:
            print e

        self._svc_syslog("%s created with uuid %s" % (st_name, str(st_uuid)))
    #_create_default_analyzer_template

    def cleanup(self):
        # TODO cleanup sandesh context
        pass
    #end cleanup

    def sandesh_si_handle_request(self, req):
        si_resp = sandesh.ServiceInstanceListResp(si_names=[])
        if req.si_name is None:
            vm_list = list(self._svc_vm_cf.get_range())
            for vm_uuid, si in vm_list:
                sandesh_si = sandesh.ServiceInstance(name = si['si_fq_str'])
                sandesh_si.vm_uuid = vm_uuid
                sandesh_si.instance_name = si['instance_name']
                if _MGMT_STR in si:
                    sandesh_si.mgmt_shared_iip = si[_MGMT_STR]
                if _LEFT_STR in si:
                    sandesh_si.left_shared_iip = si[_LEFT_STR]
                if _RIGHT_STR in si:
                    sandesh_si.right_shared_iip = si[_RIGHT_STR]

                si_resp.si_names.append(sandesh_si)
        si_resp.response(req.context())
    #end sandesh_si_handle_request

    def _utc_timestamp_usec(self):
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now-epoch
        return (delta.microseconds + (delta.seconds + delta.days * 24 * 3600) * 10**6)
    #end utc_timestamp_usec

    def _uve_svc_instance(self, si_fq_name_str, status = None, 
                          vm_uuid = None, st_name = None):
        svc_uve = UveSvcInstanceConfig(name = si_fq_name_str,
                                       deleted = False, st_name = None,
                                       vm_list = [], create_ts = None)

        if st_name:
            svc_uve.st_name = st_name
        if vm_uuid:
            svc_uve.vm_list.append(vm_uuid)
        if status:
            svc_uve.status = status
            if status == 'CREATE':
                svc_uve.create_ts = self._utc_timestamp_usec()
            elif status == 'DELETE':
                svc_uve.deleted = True

        svc_log = UveSvcInstanceConfigTrace(data=svc_uve, sandesh=self._sandesh)
        svc_log.send(sandesh=self._sandesh)
    #end uve_vm

    def _svc_syslog(self, log_msg):
        self._sandesh._logger.debug("%s", log_msg)
        vn_log = sandesh.SvcMonitorLog(log_msg = log_msg, sandesh=self._sandesh)
        vn_log.send(sandesh = self._sandesh)
    #end _svc_syslog

    def _get_proj_name_from_si_fq_str(self, si_fq_str):
        return si_fq_str.split(':')[1]
    #enf _get_si_fq_str_to_proj_name

    def _get_vn_id(self, proj_obj, vn_fq_name, 
                   shared_vn_name = None, 
                   shared_vn_subnet = None):
        vn_id = None

        if vn_fq_name:
            #search for provided VN
            try:
                vn_id = self._vnc_lib.fq_name_to_id('virtual-network', vn_fq_name)
            except NoIdError:
                self._svc_syslog("Error: vn_name %s not found" % (vn_name))
        else:
            #search or create shared VN
            domain_name, proj_name = proj_obj.get_fq_name()
            vn_fq_name = [domain_name, proj_name, shared_vn_name]
            try:
                vn_id = self._vnc_lib.fq_name_to_id('virtual-network', vn_fq_name)
            except NoIdError:
                vn_id = self._create_svc_vn(shared_vn_name, shared_vn_subnet,
                                            proj_obj)

        return vn_id
    #end _get_vn_id

    def _set_svc_vm_if_properties(self, vmi_obj, vn_obj):
        mgmt_vn = None
        left_vn = None
        right_vn = None

        #confirm service vm by checking reference to service instance
        vm_obj = self._vnc_lib.virtual_machine_read(fq_name_str = vmi_obj.parent_name)
        si_list = vm_obj.get_service_instance_refs()
        if not si_list:
            return

        #if interface property already set
        if vmi_obj.get_virtual_machine_interface_properties() != None:
            return

        fq_name = si_list[0]['to']
        si_obj = self._vnc_lib.service_instance_read(fq_name = fq_name)
        si_props = si_obj.get_service_instance_properties()
        if si_props != None:
            mgmt_vn = si_props.get_management_virtual_network()
            left_vn = si_props.get_left_virtual_network()
            right_vn = si_props.get_right_virtual_network()

        vn_fq_name_str = vn_obj.get_fq_name_str()
        if (vn_obj.name == _SVC_VN_MGMT) or (vn_fq_name_str == mgmt_vn):
            if_type = _MGMT_STR
        elif (vn_obj.name == _SVC_VN_LEFT) or (vn_fq_name_str == left_vn):
            if_type = _LEFT_STR
        elif (vn_obj.name == _SVC_VN_RIGHT) or (vn_fq_name_str == right_vn):
            if_type = _RIGHT_STR

        if_properties = VirtualMachineInterfacePropertiesType(if_type)
        vmi_obj.set_virtual_machine_interface_properties(if_properties)
        vmi_obj.set_security_group_list([])
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
    #end _set_svc_vm_if_properties

    def _create_svc_instance_vm(self, st_obj, si_obj):
        row_entry = {}
        st_props = st_obj.get_service_template_properties()
        if st_props == None:
            return 

        svc_type = st_props.get_service_type()
        image_name = st_props.get_image_name()
        if_list = st_props.get_interface_type()
        si_props = si_obj.get_service_instance_properties()
        max_instances = si_props.get_scale_out().get_max_instances()
        if image_name == None:
            self._svc_syslog("Image name not present in %s" % (st_obj.name))
            return

        #check and create service virtual networks
        nics = []
        nic_mgmt = None
        nic_left = None
        nic_right = None

        proj_fq_name = si_obj.get_parent_fq_name()
        proj_obj = self._vnc_lib.project_read(fq_name = proj_fq_name)
        for vm_if in if_list:
            nic = {}
            funcname = "get_" + vm_if.service_interface_type + "_virtual_network"
            func = getattr(si_props, funcname)
            vn_fq_name_str = func()
            vn_fq_name = None
            if vn_fq_name_str:
                domain, proj, vn_name = vn_fq_name_str.split(':')
                vn_fq_name = [domain, proj, vn_name]

            vn_id = self._get_vn_id(proj_obj, vn_fq_name,
                                    _SVC_VNS[vm_if.service_interface_type][0], 
                                    _SVC_VNS[vm_if.service_interface_type][1])
            if vn_id == None:
                continue

            nic['net-id'] = vn_id
            if vm_if.shared_ip:
                vn_obj = self._vnc_lib.virtual_network_read(id = vn_id)
                addr = self._vnc_lib.virtual_network_ip_alloc(vn_obj)
                iip_name = '%s %s' % (vn_obj.uuid, addr[0])
                iip_obj = InstanceIp(name = iip_name, instance_ip_address = addr[0])
                iip_obj.add_virtual_network(vn_obj)
                self._vnc_lib.instance_ip_create(iip_obj)
                nic['v4-fixed-ip'] = addr[0]
                row_entry[vm_if.service_interface_type] = iip_obj.uuid

            if vm_if.service_interface_type == _MGMT_STR:
                nic_mgmt = nic
            elif vm_if.service_interface_type == _LEFT_STR:
                nic_left = nic
            elif vm_if.service_interface_type == _RIGHT_STR:
                nic_right = nic

        #store nics in order [mgmt, left, right]
        if nic_mgmt:
            nics.append(nic_mgmt)
        if nic_left:
            nics.append(nic_left)
        if nic_right:
            nics.append(nic_right)

        #create and launch vm
        vm_refs = si_obj.get_virtual_machine_back_refs()
        n_client = self._novaclient_get(proj_obj.name)
        for inst_count in range(0, max_instances):
            instance_name = si_obj.name + '_' + str(inst_count + 1)
            exists = False
            for vm_ref in vm_refs or []:
                vm = n_client.servers.find(id = vm_ref['uuid'])
                if vm.name == instance_name:
                    exists = True
                    break

            if exists:
                vm_uuid = vm_ref['uuid']
            else:
                vm = self._create_svc_vm(instance_name, image_name, 
                                         nics, proj_obj.name)
                if vm == None:
                    continue
                vm_uuid = vm.id

            #store vm, instance in cassandra; use for linking when VM is up
            row_entry['si_fq_str'] = si_obj.get_fq_name_str()
            row_entry['instance_name'] = instance_name
            self._svc_vm_cf.insert(vm_uuid, row_entry)

            #uve trace
            self._uve_svc_instance(si_obj.get_fq_name_str(), status = 'CREATE', 
                                   vm_uuid = vm.id, st_name = st_obj.get_fq_name_str())
    #end _create_svc_instance_vm

    def _delete_svc_instance_vm(self, vm_uuid, proj_name, si_fq_str = None):
        found = True
        try:
            self._svc_syslog("Deleting VM %s %s" % (proj_name, vm_uuid))
            n_client = self._novaclient_get(proj_name)
            vm = n_client.servers.find(id = vm_uuid)
            vm.delete()
            self._uve_svc_instance(si_fq_str, status = 'DELETE', vm_uuid = vm_uuid)
        except nc_exc.NotFound:
            #remove from cleanup list
            self._cleanup_cf.remove(vm_uuid)
            found = False

        #remove from launch table and queue into cleanup list
        if found:
            #insert shared instance IP uuids into cleanup list if present
            try:
                svc_vm_cf_row = self._svc_vm_cf.get(vm_uuid)
                for itf_str in [_MGMT_STR, _LEFT_STR, _RIGHT_STR]:
                    if not itf_str in svc_vm_cf_row:
                        continue
                    self._cleanup_cf.insert(svc_vm_cf_row[itf_str], {'proj_name': proj_name, 'type': 'iip'})
            except pycassa.NotFoundException:
                pass

            # remove vm and add to cleanup list
            self._svc_vm_cf.remove(vm_uuid)
            self._cleanup_cf.insert(vm_uuid, {'proj_name': proj_name, 'type': 'vm'})
    #end _delete_svc_instance_vm

    def _restart_svc_vm(self, vm_uuid, si_fq_str):
        proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
        self._delete_svc_instance_vm(vm_uuid, proj_name, si_fq_str = si_fq_str)

        si_obj = self._vnc_lib.service_instance_read(fq_name_str = si_fq_str)
        st_list = si_obj.get_service_template_refs()
        if st_list != None:
            fq_name = st_list[0]['to']
            st_obj = self._vnc_lib.service_template_read(fq_name = fq_name)
            self._create_svc_instance_vm(st_obj, si_obj)
    #end _restart_svc_vm

    def _delete_shared_vn(self, vn_uuid, proj_name):
        try:
            self._svc_syslog("Deleting VN %s %s" %(proj_name, vn_uuid))
            self._vnc_lib.virtual_network_delete(id = vn_uuid)
        except NoIdError:
            #remove from cleanup list
            self._cleanup_cf.remove(vn_uuid)
    #end _delete_shared_vn

    def _delete_shared_iip(self, iip_uuid, proj_name):
        try:
            iip_obj = self._vnc_lib.instance_ip_read(id=iip_uuid)
            vmi_refs = iip_obj.get_virtual_machine_interface_refs()
            if vmi_refs == None:
                self._svc_syslog("Deleting IIP %s %s" %(proj_name, iip_uuid))
                self._vnc_lib.instance_ip_delete(id = iip_uuid)
        except NoIdError:
            #remove from cleanup list
            self._cleanup_cf.remove(iip_uuid)
    #end _delete_shared_iip

    def _delmsg_project_service_instance(self, idents):
        proj_fq_str = idents['project']
        proj_obj = self._vnc_lib.project_read(fq_name_str = proj_fq_str)
        if proj_obj.get_service_instances() != None:
            return

        #no SIs left hence delete shared VNs
        for vn_name in [_SVC_VN_MGMT, _SVC_VN_LEFT, _SVC_VN_RIGHT]:
            domain_name, proj_name = proj_obj.get_fq_name()
            vn_fq_name = [domain_name, proj_name, vn_name]
            try:
                vn_uuid = self._vnc_lib.fq_name_to_id('virtual-network', vn_fq_name)
                self._cleanup_cf.insert(vn_uuid, {'proj_name': proj_obj.name, 'type': 'vn'})
            except Exception:
                pass
    #end _delmsg_project_service_instance

    def _delmsg_service_instance_service_template(self, idents):
        si_fq_str = idents['service-instance']

        vm_list = list(self._svc_vm_cf.get_range())
        for vm_uuid, si in vm_list:
            if si_fq_str == si['si_fq_str']:
                proj_name = self._get_proj_name_from_si_fq_str(si_fq_str) 
                self._delete_svc_instance_vm(vm_uuid, proj_name, si_fq_str = si_fq_str)
    #end _delmsg_service_instance_service_template

    def _delmsg_virtual_machine_service_instance(self, idents):
        vm_uuid = idents['virtual-machine']
        si_fq_str = idents['service-instance']
        proj_name = self._get_proj_name_from_si_fq_str(si_fq_str) 
        self._delete_svc_instance_vm(vm_uuid, proj_name, si_fq_str = si_fq_str)
    #end _delmsg_virtual_machine_service_instance

    def _addmsg_virtual_machine_interface_virtual_network(self, idents):
        vmi_fq_str = idents['virtual-machine-interface'];
        vn_fq_str = idents['virtual-network'];

        try:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(fq_name_str = vmi_fq_str)
            vn_obj = self._vnc_lib.virtual_network_read(fq_name_str = vn_fq_str)
        except NoIdError:
            return

        #check if this is a service vm
        vm_obj = self._vnc_lib.virtual_machine_read(fq_name_str = vmi_obj.parent_name)
        si_list = vm_obj.get_service_instance_refs()
        if si_list:
            fq_name = si_list[0]['to']
            si_obj = self._vnc_lib.service_instance_read(fq_name = fq_name)
        else:
            try:
                svc_vm_cf_row = self._svc_vm_cf.get(vm_obj.uuid)
                si_fq_str = svc_vm_cf_row['si_fq_str']
                vm_obj.name = svc_vm_cf_row['instance_name']
                si_obj = self._vnc_lib.service_instance_read(fq_name_str  = si_fq_str)
            except pycassa.NotFoundException:
                return
            except NoIdError:
                proj_name = self._get_proj_name_from_si_fq_str(si_fq_str) 
                self._delete_svc_instance_vm(vm_obj.uuid, proj_name, si_fq_str = si_fq_str)
                return

        #create service instance to service vm link
        vm_obj.add_service_instance(si_obj)
        self._vnc_lib.virtual_machine_update(vm_obj)

        #set service instance property
        self._set_svc_vm_if_properties(vmi_obj, vn_obj)
    #end _addmsg_virtual_machine_interface_virtual_network

    def _addmsg_service_instance_service_template(self, idents):
        st_fq_str = idents['service-template'];
        si_fq_str = idents['service-instance'];

        try:
            st_obj = self._vnc_lib.service_template_read(fq_name_str = st_fq_str)
            si_obj = self._vnc_lib.service_instance_read(fq_name_str = si_fq_str)
        except NoIdError:
            return
     
        self._create_svc_instance_vm(st_obj, si_obj)
    #end _addmsg_service_instance_service_template

    def process_poll_result(self, poll_result_str):
        result_list = parse_poll_result(poll_result_str)

        #process ifmap message
        for (result_type, idents, metas) in result_list:
            for meta in metas:
                meta_name = re.sub('{.*}', '', meta.tag)
                if result_type == 'deleteResult':
                    funcname="_delmsg_"+meta_name.replace('-', '_')
                elif result_type in ['searchResult', 'updateResult']:
                    funcname="_addmsg_"+meta_name.replace('-', '_')
                #end if result_type
                try:
                    func = getattr(self, funcname)
                except AttributeError:
                    pass
                else:
                    self._svc_syslog("%s with %s/%s"% (funcname, meta_name, idents))
                    func(idents)
            #end for meta
        #end for result_type
    #end process_poll_result
  
    def _novaclient_get(self, proj_name):
        client = self._nova.get(proj_name)
        if client != None:
            return client

        self._nova[proj_name] = nc.Client('2', username = self._args.admin_user, project_id = proj_name, 
                api_key=self._args.admin_password, auth_url='http://'+ self._args.auth_host +':5000/v2.0')
        return self._nova[proj_name]
    #end _novaclient_get
 
    def _create_svc_vm(self, vm_name, image_name, nics, proj_name):
        n_client = self._novaclient_get(proj_name)
        flavor=n_client.flavors.find(ram=4096)
        image=''
        try:
            image = n_client.images.find(name = image_name)
        except nc_exc.NotFound:
            self._svc_syslog("Error: Image %s not found in project %s" % (image_name, proj_name))
            return
        except nc_exc.NoUniqueMatch:
            self._svc_syslog("Error: Multiple images %s found in project %s" % (image_name, proj_name))
            return
        
        #launch vm
        self._svc_syslog('Launching VM : ' + vm_name)
        nova_vm = n_client.servers.create(name=vm_name, image=image, 
                                          flavor=flavor, nics=nics)
        nova_vm.get()
        self._svc_syslog('Created VM : ' + str(nova_vm))
        return nova_vm
    #end create_svc_vm

    def _create_svc_vn(self, vn_name, vn_subnet, proj_obj):
        self._svc_syslog("Creating network %s subnet %s" %(vn_name, vn_subnet))

        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj)
        domain_name, project_name = proj_obj.get_fq_name()
        ipam_fq_name = [domain_name, 'default-project', 'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        cidr = vn_subnet.split('/')
        pfx = cidr[0]
        pfx_len = int(cidr[1])
        subnet_info = IpamSubnetType(subnet=SubnetType(pfx, pfx_len))
        subnet_data = VnSubnetsType([subnet_info])                                                      
        vn_obj.add_network_ipam(ipam_obj, subnet_data)    
        self._vnc_lib.virtual_network_create(vn_obj)

        return vn_obj.uuid
    #end _create_svc_vn         

    def _cassandra_init(self):
        sys_mgr = SystemManager(self._args.cassandra_server_list[0])

        if self._args.reset_config:
            try:
                sys_mgr.drop_keyspace(SvcMonitor._KEYSPACE)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                print "Warning! " + str(e)

        try:
            sys_mgr.create_keyspace(SvcMonitor._KEYSPACE, SIMPLE_STRATEGY,
                                    {'replication_factor': '1'})
        except pycassa.cassandra.ttypes.InvalidRequestException as e:
            print "Warning! " + str(e)

        column_families = [self._SVC_VM_CF, self._CLEANUP_CF]
        for cf in column_families:
            try:
                sys_mgr.create_column_family(SvcMonitor._KEYSPACE, cf)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                print "Warning! " + str(e)

        conn_pool = pycassa.ConnectionPool(SvcMonitor._KEYSPACE,
                                           self._args.cassandra_server_list)
        self._svc_vm_cf = pycassa.ColumnFamily(conn_pool, self._SVC_VM_CF)
        self._cleanup_cf = pycassa.ColumnFamily(conn_pool, self._CLEANUP_CF)
    #end _cassandra_init

#end class svc_monitor

def launch_arc(monitor, ssrc_mapc):
    arc_mapc = arc_initialize(monitor._args, ssrc_mapc)
    while True:
        try:
            pollreq = PollRequest(arc_mapc.get_session_id())
            result = arc_mapc.call('poll', pollreq)
            monitor.process_poll_result(result)
        except Exception as e:
            if type(e) == socket.error:
                time.sleep(3)
            else:
                cgitb.Hook(format = "text", file = open(monitor._tmp_file, 'w')).handle(sys.exc_info())
                fhandle = open(monitor._tmp_file)
                monitor._svc_err_logger.error("%s" % fhandle.read())
#end launch_arc

def launch_ssrc(monitor):
    ssrc_mapc = ssrc_initialize(monitor._args)
    arc_glet = gevent.spawn(launch_arc, monitor, ssrc_mapc)
    arc_glet.join()
#end launch_ssrc

def timer_callback(monitor):
    #check health of VMs
    try:
        vm_list = list(monitor._svc_vm_cf.get_range())
    except Exception as e:
        return

    for vm_uuid, si in vm_list:
        try:
            proj_name = monitor._get_proj_name_from_si_fq_str(si['si_fq_str'])
            n_client = monitor._novaclient_get(proj_name)
            server = n_client.servers.find(id = vm_uuid)
        except nc_exc.NotFound:
            continue
        except Exception as e:
            continue
        else: 
            if server.status == 'ERROR':
                monitor._restart_svc_vm(vm_uuid, si['si_fq_str'])
#end timer_callback

def launch_timer(monitor):
    while True:
        gevent.sleep(_CHECK_SVC_VM_HEALTH_INTERVAL)
        try:
            timer_callback(monitor)
        except Exception as e:
            cgitb.Hook(format = "text", file = open(monitor._tmp_file, 'w')).handle(sys.exc_info())
            fhandle = open(monitor._tmp_file)
            monitor._svc_err_logger.error("%s" % fhandle.read())
#end launch_timer

def cleanup_callback(monitor):
    try:
        delete_list = list(monitor._cleanup_cf.get_range())
    except Exception as e:
        return

    for uuid, info in delete_list or []:
        if info['type'] == 'vm':
            monitor._delete_svc_instance_vm(uuid, info['proj_name'])
        elif info['type'] == 'vn':
            monitor._delete_shared_vn(uuid, info['proj_name'])
        elif info['type'] == 'iip':
            monitor._delete_shared_iip(uuid, info['proj_name'])
#end cleanup_callback

def launch_cleanup(monitor):
    while True:
        gevent.sleep(_CHECK_CLEANUP_INTERVAL)
        try:
            cleanup_callback(monitor)
        except Exception as e:
            cgitb.Hook(format = "text", file = open(monitor._tmp_file, 'w')).handle(sys.exc_info())
            fhandle = open(monitor._tmp_file)
            monitor._svc_err_logger.error("%s" % fhandle.read())
#end launch_cleanup

def parse_args(args_str):
    '''
    Eg. python svc_monitor.py --ifmap_server_ip 192.168.1.17 
                         --ifmap_server_port 8443
                         --ifmap_username test
                         --ifmap_password test
                         --cassandra_server_list 10.1.2.3:9160
                         --api_server_ip 10.1.2.3
                         --api_server_port 8082
                         --collector 127.0.0.1
                         --collector_port 8080
                         --disc_server_ip 127.0.0.1
                         --disc_server_port 5998
                         --http_server_port 8090
                         --log_local
                         --log_level SYS_DEBUG
                         --log_category test
                         --log_file <stdout>
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we show all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help = False)
    
    conf_parser.add_argument("-c", "--conf_file",
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'ifmap_server_ip' : '127.0.0.1',
        'ifmap_server_port' : '8443',
        'ifmap_username' : 'test2',
        'ifmap_password' : 'test2',
        'cassandra_server_list' : '127.0.0.1:9160',
        'api_server_ip' : '127.0.0.1',
        'api_server_port' : '8082',
        'collector' : None,
        'collector_port' : None,
        'disc_server_ip' : None,
        'disc_server_port' : None,
        'http_server_port' : '8088',
        'log_local' : False,
        'log_level' : SandeshLevel.SYS_DEBUG,
        'log_category' : '',
        'log_file' : Sandesh._DEFAULT_LOG_FILE,
        }
    secopts = {
        'use_certs': False,
        'keyfile'  : '',
        'certfile' : '',
        'ca_certs' : '',
        'ifmap_certauth_port' : "8444",
        }
    ksopts = {
        'auth_host'        : '127.0.0.1',
        'admin_user'       : 'user1',
        'admin_password'   : 'password1',
        'admin_tenant_name': 'default-domain'
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read([args.conf_file])
        defaults.update(dict(config.items("DEFAULTS")))
        if 'SECURITY' in config.sections() and 'use_certs' in config.options('SECURITY'):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
        )
    defaults.update(secopts)
    defaults.update(ksopts)
    parser.set_defaults(**defaults)

    parser.add_argument("--ifmap_server_ip", help = "IP address of ifmap server")
    parser.add_argument("--ifmap_server_port", help = "Port of ifmap server")

    # TODO should be from certificate
    parser.add_argument("--ifmap_username",
                        help = "Username known to ifmap server")
    parser.add_argument("--ifmap_password",
                        help = "Password known to ifmap server")
    parser.add_argument("--cassandra_server_list",
                        help = "List of cassandra servers in IP Address:Port format",
                        nargs = '+')
    parser.add_argument("--reset_config", action = "store_true",
                        help = "Warning! Destroy previous configuration and start clean")
    parser.add_argument("--api_server_ip",
                        help = "IP address of API server")
    parser.add_argument("--api_server_port",
                        help = "Port of API server")
    parser.add_argument("--collector",
                        help = "IP address of VNC collector server")
    parser.add_argument("--collector_port",
                        help = "Port of VNC collector server")
    parser.add_argument("--disc_server_ip",
                        help = "IP address of the discovery server")
    parser.add_argument("--disc_server_port",
                        help = "Port of the discovery server")
    parser.add_argument("--http_server_port",
                        help = "Port of local HTTP server")
    parser.add_argument("--log_local", action = "store_true",
                        help = "Enable local logging of sandesh messages")
    parser.add_argument("--log_level",
                        help = "Severity level for local logging of sandesh messages")
    parser.add_argument("--log_category",
                        help = "Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help = "Filename for the logs to be written to")
    parser.add_argument("--admin_user",
                        help = "Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help = "Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help = "Tenamt name for keystone admin user")
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    return args
#end parse_args
    
def main(args_str = None):
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)

    # Retry till API server is up
    connected = False
    while not connected:
        try:
            vnc_api = VncApi(args.admin_user, args.admin_password, args.admin_tenant_name,
                             args.api_server_ip, args.api_server_port)
            connected = True
        except requests.exceptions.ConnectionError as e:
            time.sleep(3)

    monitor = SvcMonitor(vnc_api, args)
    ssrc_task = gevent.spawn(launch_ssrc, monitor)
    timer_task = gevent.spawn(launch_timer, monitor)
    cleanup_task = gevent.spawn(launch_cleanup, monitor)
    gevent.joinall([ssrc_task, timer_task, cleanup_task])
#end main

if __name__ == '__main__':
    cgitb.enable(format='text')
    main()
