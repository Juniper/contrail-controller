#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor to instantiate/scale/monitor services like firewall, LB, ...
"""

import gevent
from cfgm_common.zkclient import ZookeeperClient
from gevent import monkey
monkey.patch_all()
import sys
import requests
import ConfigParser
import cgitb
import logging
import logging.handlers
import argparse
import socket
import time
import datetime

import re
import os

import pycassa
from pycassa.system_manager import *

from cfgm_common.imid import *
from cfgm_common import vnc_cpu_info

from vnc_api.vnc_api import *

from pysandesh.sandesh_base import Sandesh
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.service_instance.ttypes import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT
from sandesh.svc_mon_introspect import ttypes as sandesh

# nova imports
from novaclient import client as nc
from novaclient import exceptions as nc_exc

import discoveryclient.client as client

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

# zookeeper client connection
_zookeeper_client = None

class SvcMonitor(object):

    """
    data + methods used/referred to by ssrc and arc greenlets
    """

    _KEYSPACE = 'svc_monitor_keyspace'
    _SVC_VM_CF = 'svc_vm_table'
    _SVC_SI_CF = 'svc_si_table'
    _SVC_CLEANUP_CF = 'svc_cleanup_table'

    def __init__(self, vnc_lib, args=None):
        self._args = args

        # api server and cassandra init
        self._vnc_lib = vnc_lib
        self._cassandra_init()

        # dictionary for nova
        self._nova = {}

        #initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(self._args.disc_server_ip,
                                                self._args.disc_server_port,
                                                client_type='Service Monitor')

        #sandesh init
        self._sandesh = Sandesh()
        sandesh.ServiceInstanceList.handle_request =\
            self.sandesh_si_handle_request
        module = Module.SVC_MONITOR
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        instance_id = INSTANCE_ID_DEFAULT
        self._sandesh.init_generator(
            module_name, socket.gethostname(), node_type_name, instance_id,
            self._args.collectors, 'svc_monitor_context',
            int(self._args.http_server_port), ['cfgm_common', 'sandesh'],
            self._disc)
        self._sandesh.set_logging_params(enable_local_log=self._args.log_local,
                                         category=self._args.log_category,
                                         level=self._args.log_level,
                                         file=self._args.log_file)

        #create default analyzer template
        self._create_default_template('analyzer-template', 'analyzer',
                                      'analyzer')
        self._create_default_template('nat-template', 'nat-service',
                                      'firewall', 'in-network-nat')

        #create cpu_info object to send periodic updates
        sysinfo_req = False
        cpu_info = vnc_cpu_info.CpuInfo(
            module_name, instance_id, sysinfo_req, self._sandesh, 60)
        self._cpu_info = cpu_info

        # logging
        self._err_file = '/var/log/contrail/svc-monitor.err'
        self._tmp_file = '/var/log/contrail/svc-monitor.tmp'
        self._svc_err_logger = logging.getLogger('SvcErrLogger')
        self._svc_err_logger.setLevel(logging.ERROR)
        handler = logging.handlers.RotatingFileHandler(
            self._err_file, maxBytes=64*1024, backupCount=2)
        self._svc_err_logger.addHandler(handler)
    # end __init__

    # create service template
    def _create_default_template(self, st_name, image_name,
                                 svc_type, svc_mode=None):
        domain_name = 'default-domain'
        domain_fq_name = [domain_name]
        st_fq_name = [domain_name, st_name]
        self._svc_syslog("Creating %s %s image %s" %
                         (domain_name, st_name, image_name))

        try:
            st_obj = self._vnc_lib.service_template_read(fq_name=st_fq_name)
            st_uuid = st_obj.uuid
            self._svc_syslog("%s exists uuid %s" % (st_name, str(st_uuid)))
            return
        except NoIdError:
            domain = self._vnc_lib.domain_read(fq_name=domain_fq_name)
            st_obj = ServiceTemplate(name=st_name, domain_obj=domain)
            st_uuid = self._vnc_lib.service_template_create(st_obj)

        svc_properties = ServiceTemplateType()
        svc_properties.set_image_name(image_name)
        svc_properties.set_service_type(svc_type)
        svc_properties.set_flavor("m1.medium")
        svc_properties.set_ordered_interfaces(True)

        # set interface list
        if svc_type == 'analyzer':
            if_list = [['left', False]]
        else:
            if_list = [
                ['management', False], ['left', False], ['right', False]]
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
    # end cleanup

    def _sandesh_populate_vn_info(self, si_info, sandesh_si):
        for if_str in [_LEFT_STR, _RIGHT_STR, _MGMT_STR]:
            if_set = set()
            if_str_vn = if_str + '-vn'
            if not if_str_vn in si_info.keys():
                continue

            vn_fq_str = str(si_info[if_str_vn])
            vn_uuid = str(si_info[vn_fq_str])
            vn_str = ("VN [%s : %s]" % (vn_fq_str, vn_uuid))
            if_set.add(vn_str)

            iip_uuid_str = if_str + '-iip-uuid'
            if iip_uuid_str in si_info.keys():
                vn_iip_uuid = str(si_info[iip_uuid_str])
                iip_addr_str = if_str + '-iip-addr'
                vn_iip_addr = str(si_info[iip_addr_str])
                iip_str = ("IIP [%s : %s]" % (vn_iip_addr, vn_iip_uuid))
                if_set.add(iip_str)

            if if_str == _LEFT_STR:
                sandesh_si.left_vn = list(if_set)
            if if_str == _RIGHT_STR:
                sandesh_si.right_vn = list(if_set)
            if if_str == _MGMT_STR:
                sandesh_si.management_vn = list(if_set)

            si_info['done'] = True
    # end _sandesh_populate_vn_info

    def sandesh_si_handle_request(self, req):
        si_resp = sandesh.ServiceInstanceListResp(si_names=[])
        if req.si_name is None:
            vm_list = list(self._svc_vm_cf.get_range())
            si_list = list(self._svc_si_cf.get_range())

            #walk all vms
            for vm_uuid, si in vm_list:
                if 'done' in si:
                    continue

                #collect all ecmp instances
                sandesh_si = sandesh.ServiceInstance(name=si['si_fq_str'])
                vm_set = set()
                for key, val in vm_list:
                    if val['si_fq_str'] != si['si_fq_str']:
                        continue
                    vm_str = ("%s: %s" % (val['instance_name'], key))
                    vm_set.add(vm_str)
                    val['done'] = True
                sandesh_si.vm_list = list(vm_set)

                #find the vn and iip iformation
                for si_fq_str, si_info in si_list:
                    if si_fq_str != si['si_fq_str']:
                        continue
                    self._sandesh_populate_vn_info(si_info, sandesh_si)
                    si_info['done'] = True
                si_resp.si_names.append(sandesh_si)

            #walk all instances where vms are pending launch
            for si_fq_str, si_info in si_list:
                if 'done' in si_info.keys():
                    continue
                sandesh_si = sandesh.ServiceInstance(name=si_fq_str)
                sandesh_si.vm_list = set()
                sandesh_si.instance_name = ''
                self._sandesh_populate_vn_info(si_info, sandesh_si)
                si_resp.si_names.append(sandesh_si)

        si_resp.response(req.context())
    # end sandesh_si_handle_request

    def _utc_timestamp_usec(self):
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now - epoch
        return (delta.microseconds +
                (delta.seconds + delta.days * 24 * 3600) * 10 ** 6)
    # end utc_timestamp_usec

    def _uve_svc_instance(self, si_fq_name_str, status=None,
                          vm_uuid=None, st_name=None):
        svc_uve = UveSvcInstanceConfig(name=si_fq_name_str,
                                       deleted=False, st_name=None,
                                       vm_list=[], create_ts=None)

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

        svc_log = UveSvcInstanceConfigTrace(
            data=svc_uve, sandesh=self._sandesh)
        svc_log.send(sandesh=self._sandesh)
    # end uve_vm

    def _svc_syslog(self, log_msg):
        self._sandesh._logger.debug("%s", log_msg)
        vn_log = sandesh.SvcMonitorLog(
            log_msg=log_msg, sandesh=self._sandesh)
        vn_log.send(sandesh=self._sandesh)
    # end _svc_syslog

    def _get_proj_name_from_si_fq_str(self, si_fq_str):
        return si_fq_str.split(':')[1]
    # enf _get_si_fq_str_to_proj_name

    def _get_vn_id(self, proj_obj, vn_fq_name_str,
                   shared_vn_name=None,
                   shared_vn_subnet=None):
        vn_id = None

        if vn_fq_name_str:
            vn_fq_name = vn_fq_name_str.split(':')
            # search for provided VN
            try:
                vn_id = self._vnc_lib.fq_name_to_id(
                    'virtual-network', vn_fq_name)
            except NoIdError:
                self._svc_syslog("Error: vn_fq_name %s not found" % (vn_fq_name_str))
        else:
            # search or create shared VN
            domain_name, proj_name = proj_obj.get_fq_name()
            vn_fq_name = [domain_name, proj_name, shared_vn_name]
            try:
                vn_id = self._vnc_lib.fq_name_to_id(
                    'virtual-network', vn_fq_name)
            except NoIdError:
                vn_id = self._create_svc_vn(shared_vn_name, shared_vn_subnet,
                                            proj_obj)

        return vn_id
    # end _get_vn_id

    def _set_svc_vm_if_properties(self, vmi_obj, vn_obj):
        # confirm service vm by checking reference to service instance
        vm_id = get_vm_id_from_interface(vmi_obj)
        if vm_id is None:
            return
        vm_obj = self._vnc_lib.virtual_machine_read(id=vm_id)
        si_list = vm_obj.get_service_instance_refs()
        if not si_list:
            return

        # if interface property already set
        if vmi_obj.get_virtual_machine_interface_properties() is not None:
            return

        # get service instance
        fq_name = si_list[0]['to']
        si_obj = self._vnc_lib.service_instance_read(fq_name=fq_name)
        si_props = si_obj.get_service_instance_properties()
        si_if_list = si_props.get_interface_list()
        si_if = None

        # get service template
        st_list = si_obj.get_service_template_refs()
        if st_list is not None:
            fq_name = st_list[0]['to']
            st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)
        st_props = st_obj.get_service_template_properties()
        st_if_list = st_props.get_interface_type()

        # set interface type
        vn_fq_name_str = vn_obj.get_fq_name_str()
        for idx in range(0, len(st_if_list)):
            st_if = st_if_list[idx]
            itf_type = st_if.service_interface_type

            si_if = None
            if si_if_list and st_props.get_ordered_interfaces():
                si_if = si_if_list[idx]
                si_vn_str = si_if.get_virtual_network()
            else:
                funcname = "get_" + itf_type + "_virtual_network"
                func = getattr(si_props, funcname)
                si_vn_str = func()

            if (((itf_type == _MGMT_STR) and (vn_obj.name == _SVC_VN_MGMT))
                    or ((itf_type == _LEFT_STR) and
                        (vn_obj.name == _SVC_VN_LEFT))
                    or ((itf_type == _RIGHT_STR) and
                        (vn_obj.name == _SVC_VN_RIGHT))
                    or ((si_vn_str == vn_fq_name_str))):
                if_properties = VirtualMachineInterfacePropertiesType(itf_type)
                vmi_obj.set_virtual_machine_interface_properties(if_properties)
                break

        # set static routes
        if st_if.get_static_route_enable():
            static_routes = si_if.get_static_routes()
            if not static_routes:
                static_routes = {'route':[]}

            try:
                domain_name, proj_name = si_obj.get_parent_fq_name()
                rt_name = si_obj.uuid + ' ' + str(idx)
                rt_fq_name = [domain_name, proj_name, rt_name]
                rt_obj = self._vnc_lib.interface_route_table_read(
                    fq_name=rt_fq_name)
                rt_obj.set_interface_route_table_routes(static_routes)
            except NoIdError:
                proj_obj = self._vnc_lib.project_read(
                    fq_name=si_obj.get_parent_fq_name())
                rt_obj = InterfaceRouteTable(
                    name=rt_name,
                    parent_obj=proj_obj,
                    interface_route_table_routes=static_routes)
                self._vnc_lib.interface_route_table_create(rt_obj)
            vmi_obj.set_interface_route_table(rt_obj)

        # remove security groups and update vmi
        if st_props.service_mode not in ['in-network', 'in-network-nat'] or st_props.service_type == 'analyzer':
            vmi_obj.set_security_group_list([])
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
    # end _set_svc_vm_if_properties

    def _create_svc_instance_vm(self, st_obj, si_obj):
        #check if all config received before launch
        if not self._check_store_si_info(st_obj, si_obj):
            return

        row_entry = {}
        st_props = st_obj.get_service_template_properties()
        if st_props is None:
            return
        st_if_list = st_props.get_interface_type()

        flavor = st_props.get_flavor()
        image_name = st_props.get_image_name()
        if image_name is None:
            self._svc_syslog("Error: Image name not present in %s" %
                             (st_obj.name))
            return

        si_props = si_obj.get_service_instance_properties()
        max_instances = si_props.get_scale_out().get_max_instances()
        si_if_list = si_props.get_interface_list()
        if si_if_list and (len(si_if_list) != len(st_if_list)):
            self._svc_syslog("Error: IF mismatch template %s instance %s" %
                             (len(st_if_list), len(si_if_list)))
            return

        # check and create service virtual networks
        nics = []
        proj_fq_name = si_obj.get_parent_fq_name()
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        for idx in range(0, len(st_if_list)):
            nic = {}
            st_if = st_if_list[idx]
            itf_type = st_if.service_interface_type

            # set vn id
            if si_if_list and st_props.get_ordered_interfaces():
                si_if = si_if_list[idx]
                vn_fq_name_str = si_if.get_virtual_network()
            else:
                funcname = "get_" + itf_type + "_virtual_network"
                func = getattr(si_props, funcname)
                vn_fq_name_str = func()

            if itf_type in _SVC_VNS:
                vn_id = self._get_vn_id(proj_obj, vn_fq_name_str,
                                        _SVC_VNS[itf_type][0],
                                        _SVC_VNS[itf_type][1])
            else:
                vn_id = self._get_vn_id(proj_obj, vn_fq_name_str)
            if vn_id is None:
                continue
            nic['net-id'] = vn_id
            nic['v4-fixed-ip'] = None

            # set shared ip
            if st_if.shared_ip:
                try:
                    iip_dict = self._svc_si_cf.get(si_obj.get_fq_name_str())
                    iip_uuid_str = itf_type + '-iip-uuid'
                    if iip_uuid_str in iip_dict.keys():
                        nic['v4-fixed-ip'] = iip_dict[itf_type + '-iip-addr']
                except pycassa.NotFoundException:
                    self._svc_syslog(
                        "Error: Shared IP not found in db for %s" % (itf_type))

            # add to nic list
            nics.append(nic)

        # create and launch vm
        vm_refs = si_obj.get_virtual_machine_back_refs()
        n_client = self._novaclient_get(proj_obj.name)
        for inst_count in range(0, max_instances):
            instance_name = si_obj.name + '_' + str(inst_count + 1)
            exists = False
            for vm_ref in vm_refs or []:
                vm = n_client.servers.find(id=vm_ref['uuid'])
                if vm.name == instance_name:
                    exists = True
                    break

            if exists:
                vm_uuid = vm_ref['uuid']
            else:
                vm = self._create_svc_vm(instance_name, image_name,
                                         nics, proj_obj.name, flavor)
                if vm is None:
                    continue
                vm_uuid = vm.id

            # store vm, instance in cassandra; use for linking when VM is up
            row_entry['si_fq_str'] = si_obj.get_fq_name_str()
            row_entry['instance_name'] = instance_name
            self._svc_vm_cf.insert(vm_uuid, row_entry)

            # uve trace
            self._uve_svc_instance(si_obj.get_fq_name_str(),
                                   status='CREATE', vm_uuid=vm.id,
                                   st_name=st_obj.get_fq_name_str())
    # end _create_svc_instance_vm

    def _delete_svc_instance_vm(self, vm_uuid, proj_name, si_fq_str=None):
        found = True
        try:
            self._svc_syslog("Deleting VM %s %s" % (proj_name, vm_uuid))
            n_client = self._novaclient_get(proj_name)
            vm = n_client.servers.find(id=vm_uuid)
            vm.delete()
            self._uve_svc_instance(
                si_fq_str, status='DELETE', vm_uuid=vm_uuid)
        except nc_exc.NotFound:
            # remove from cleanup list
            self._cleanup_cf.remove(vm_uuid)
            found = False

        # remove from launch table and queue into cleanup list
        if found:
            self._svc_vm_cf.remove(vm_uuid)
            self._cleanup_cf.insert(
                vm_uuid, {'proj_name': proj_name, 'type': 'vm'})
    # end _delete_svc_instance_vm

    def _restart_svc_vm(self, vm_uuid, si_fq_str):
        proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
        self._delete_svc_instance_vm(vm_uuid, proj_name, si_fq_str=si_fq_str)

        si_obj = self._vnc_lib.service_instance_read(fq_name_str=si_fq_str)
        st_list = si_obj.get_service_template_refs()
        if st_list is not None:
            fq_name = st_list[0]['to']
            st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)
            self._create_svc_instance_vm(st_obj, si_obj)
    # end _restart_svc_vm

    def _check_store_si_info(self, st_obj, si_obj):
        config_complete = True
        st_props = st_obj.get_service_template_properties()
        st_if_list = st_props.get_interface_type()
        si_props = si_obj.get_service_instance_properties()
        si_if_list = si_props.get_interface_list()
        if si_if_list and (len(si_if_list) != len(st_if_list)):
            self._svc_syslog("Error: IF mismatch template %s instance %s" %
                             (len(st_if_list), len(si_if_list)))
            return

        #read existing si_entry
        try:
            si_entry = self._svc_si_cf.get(si_obj.get_fq_name_str())
        except Exception:
            si_entry = {}

        #walk the interface list
        for idx in range(0, len(st_if_list)):
            st_if = st_if_list[idx]
            itf_type = st_if.service_interface_type

            si_if = None
            if si_if_list and st_props.get_ordered_interfaces():
                si_if = si_if_list[idx]
                si_vn_str = si_if.get_virtual_network()
            else:
                funcname = "get_" + itf_type + "_virtual_network"
                func = getattr(si_props, funcname)
                si_vn_str = ':'.join(func())
            if not si_vn_str:
                continue

            si_entry[itf_type + '-vn'] = si_vn_str
            try:
                vn_obj = self._vnc_lib.virtual_network_read(
                    fq_name_str=si_vn_str)
                if vn_obj.uuid != si_entry.get(si_vn_str, None):
                    si_entry[si_vn_str] = vn_obj.uuid

                if not st_if.shared_ip:
                    continue

                iip_uuid_str = itf_type + '-iip-uuid'
                iip_uuid = si_entry.get(iip_uuid_str, None)
                iip = self._allocate_shared_iip(st_obj, si_obj,
                                                vn_obj, iip_uuid)
                si_entry[itf_type + '-iip-uuid'] = iip['uuid']
                si_entry[itf_type + '-iip-addr'] = iip['addr']
            except NoIdError:
                self._svc_syslog("Warn: VN %s add is pending" % si_vn_str)
                si_entry[si_vn_str] = 'pending'
                config_complete = False

        if config_complete:
            self._svc_syslog("SI %s info is complete" %
                             si_obj.get_fq_name_str())
        else:
            self._svc_syslog("Warn: SI %s info is not complete" %
                             si_obj.get_fq_name_str())

        #insert entry
        self._svc_si_cf.insert(si_obj.get_fq_name_str(), si_entry)
        return config_complete
    #end _check_store_si_info

    def _allocate_shared_iip(self, st_obj, si_obj, vn_obj, iip_uuid):
        iip_entry = {}
        iip_obj = None

        if iip_uuid:
            try:
                iip_obj = self._vnc_lib.instance_ip_read(id=iip_uuid)
            except NoIdError:
                pass

        # allocate ip
        if not iip_obj:
            addr = self._vnc_lib.virtual_network_ip_alloc(vn_obj)
            iip_name = '%s %s' % (vn_obj.uuid, addr[0])
            iip_obj = InstanceIp(name=iip_name,
                                 instance_ip_address=addr[0])
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)

        iip_entry = {'uuid': iip_obj.uuid,
                     'addr': iip_obj.get_instance_ip_address()}

        return iip_entry
    #end _allocate_shared_iip

    def _delete_shared_vn(self, vn_uuid, proj_name):
        try:
            self._svc_syslog("Deleting VN %s %s" % (proj_name, vn_uuid))
            self._vnc_lib.virtual_network_delete(id=vn_uuid)
        except RefsExistError:
            self._svc_err_logger.error("Delete failed refs exist VN %s %s" %
                                       (proj_name, vn_uuid))
        except NoIdError:
            # remove from cleanup list
            self._cleanup_cf.remove(vn_uuid)
    # end _delete_shared_vn

    def _delete_shared_iip(self, iip_uuid, proj_name):
        try:
            iip_obj = self._vnc_lib.instance_ip_read(id=iip_uuid)
            vmi_refs = iip_obj.get_virtual_machine_interface_refs()
            if vmi_refs is None:
                self._svc_syslog("Deleting IIP %s %s" % (proj_name, iip_uuid))
                self._vnc_lib.instance_ip_delete(id=iip_uuid)
        except RefsExistError:
            self._svc_err_logger.error("Delete failed refs exist IIP %s %s" %
                                       (proj_name, iip_uuid))
        except NoIdError:
            # remove from cleanup list
            self._cleanup_cf.remove(iip_uuid)
    # end _delete_shared_iip

    def _delmsg_project_service_instance(self, idents):
        proj_fq_str = idents['project']
        proj_obj = self._vnc_lib.project_read(fq_name_str=proj_fq_str)
        if proj_obj.get_service_instances() is not None:
            return

        # no SIs left hence delete shared VNs
        for vn_name in [_SVC_VN_MGMT, _SVC_VN_LEFT, _SVC_VN_RIGHT]:
            domain_name, proj_name = proj_obj.get_fq_name()
            vn_fq_name = [domain_name, proj_name, vn_name]
            try:
                vn_uuid = self._vnc_lib.fq_name_to_id(
                    'virtual-network', vn_fq_name)
                self._cleanup_cf.insert(
                    vn_uuid, {'proj_name': proj_obj.name, 'type': 'vn'})
            except Exception:
                pass
    # end _delmsg_project_service_instance

    def _delmsg_service_instance_service_template(self, idents):
        si_fq_str = idents['service-instance']

        vm_list = list(self._svc_vm_cf.get_range())
        for vm_uuid, si in vm_list:
            if si_fq_str != si['si_fq_str']:
                continue

            proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
            self._delete_svc_instance_vm(
                vm_uuid, proj_name, si_fq_str=si_fq_str)

            #insert shared instance IP uuids into cleanup list if present
            try:
                si_info = self._svc_si_cf.get(si_fq_str)
                for itf_str in [_MGMT_STR, _LEFT_STR, _RIGHT_STR]:
                    iip_uuid_str = itf_str + '-iip-uuid'
                    if not iip_uuid_str in si_info:
                        continue
                    self._cleanup_cf.insert(
                        si_info[iip_uuid_str], {'proj_name': proj_name,
                                                'type': 'iip'})
            except pycassa.NotFoundException:
                pass

        #delete si info
        try:
            self._svc_si_cf.remove(si_fq_str)
        except pycassa.NotFoundException:
            pass
    #end _delmsg_service_instance_service_template

    def _delmsg_virtual_machine_service_instance(self, idents):
        vm_uuid = idents['virtual-machine']
        si_fq_str = idents['service-instance']
        proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
        self._delete_svc_instance_vm(vm_uuid, proj_name, si_fq_str=si_fq_str)
    # end _delmsg_service_instance_virtual_machine

    def _delmsg_virtual_machine_interface_route_table(self, idents):
        rt_fq_str = idents['interface-route-table']

        rt_obj = self._vnc_lib.interface_route_table_read(
            fq_name_str=rt_fq_str)
        vmi_list = rt_obj.get_virtual_machine_interface_back_refs()
        if vmi_list is None:
            self._vnc_lib.interface_route_table_delete(id=rt_obj.uuid)
    # end _delmsg_virtual_machine_interface_route_table

    def _addmsg_virtual_machine_interface_virtual_network(self, idents):
        vmi_fq_str = idents['virtual-machine-interface']
        vn_fq_str = idents['virtual-network']

        try:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                fq_name_str=vmi_fq_str)
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name_str=vn_fq_str)
        except NoIdError:
            return

        # check if this is a service vm
        vm_id = get_vm_id_from_interface(vmi_obj)
        if vm_id is None:
            return
        vm_obj = self._vnc_lib.virtual_machine_read(id=vm_id)
        si_list = vm_obj.get_service_instance_refs()
        if si_list:
            fq_name = si_list[0]['to']
            si_obj = self._vnc_lib.service_instance_read(fq_name=fq_name)
        else:
            try:
                svc_vm_cf_row = self._svc_vm_cf.get(vm_obj.uuid)
                si_fq_str = svc_vm_cf_row['si_fq_str']
                vm_obj.name = svc_vm_cf_row['instance_name']
                si_obj = self._vnc_lib.service_instance_read(
                    fq_name_str=si_fq_str)
            except pycassa.NotFoundException:
                return
            except NoIdError:
                proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
                self._delete_svc_instance_vm(
                    vm_obj.uuid, proj_name, si_fq_str=si_fq_str)
                return

        # create service instance to service vm link
        vm_obj.add_service_instance(si_obj)
        self._vnc_lib.virtual_machine_update(vm_obj)

        # set service instance property
        self._set_svc_vm_if_properties(vmi_obj, vn_obj)

    # end _addmsg_virtual_machine_interface_virtual_network

    def _addmsg_service_instance_service_template(self, idents):
        st_fq_str = idents['service-template']
        si_fq_str = idents['service-instance']

        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name_str=st_fq_str)
            si_obj = self._vnc_lib.service_instance_read(
                fq_name_str=si_fq_str)
        except NoIdError:
            return

        #launch VMs
        self._create_svc_instance_vm(st_obj, si_obj)
    # end _addmsg_service_instance_service_template

    def _addmsg_service_instance_properties(self, idents):
        si_fq_str = idents['service-instance']

        try:
            si_obj = self._vnc_lib.service_instance_read(
                fq_name_str=si_fq_str)
        except NoIdError:
            return

        #update static routes
        self._update_static_routes(si_obj)
    # end _addmsg_service_instance_service_template

    def _addmsg_project_virtual_network(self, idents):
        vn_fq_str = idents['virtual-network']

        try:
            si_list = list(self._svc_si_cf.get_range())
        except pycassa.NotFoundException:
            return

        for si_fq_str, si_info in si_list:
            if vn_fq_str not in si_info.keys():
                continue

            try:
                si_obj = self._vnc_lib.service_instance_read(
                    fq_name_str=si_fq_str)
                st_refs = si_obj.get_service_template_refs()
                fq_name = st_refs[0]['to']
                st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)

                #launch VMs
                self._create_svc_instance_vm(st_obj, si_obj)
            except Exception:
                continue
    #end _addmsg_project_virtual_network

    def process_poll_result(self, poll_result_str):
        result_list = parse_poll_result(poll_result_str)

        # process ifmap message
        for (result_type, idents, metas) in result_list:
            for meta in metas:
                meta_name = re.sub('{.*}', '', meta.tag)
                if result_type == 'deleteResult':
                    funcname = "_delmsg_" + meta_name.replace('-', '_')
                elif result_type in ['searchResult', 'updateResult']:
                    funcname = "_addmsg_" + meta_name.replace('-', '_')
                # end if result_type
                try:
                    func = getattr(self, funcname)
                except AttributeError:
                    pass
                else:
                    self._svc_syslog("%s with %s/%s"
                                     % (funcname, meta_name, idents))
                    func(idents)
            # end for meta
        # end for result_type
    # end process_poll_result

    def _novaclient_get(self, proj_name):
        client = self._nova.get(proj_name)
        if client is not None:
            return client

        self._nova[proj_name] = nc.Client(
            '2', username=self._args.admin_user, project_id=proj_name,
            api_key=self._args.admin_password,
            region_name=self._args.region_name, service_type='compute',
            auth_url='http://' + self._args.auth_host + ':5000/v2.0')
        return self._nova[proj_name]
    # end _novaclient_get

    def _update_static_routes(self, si_obj):
        # get service instance interface list
        si_props = si_obj.get_service_instance_properties()
        si_if_list = si_props.get_interface_list()
        if not si_if_list:
            return

        for idx in range(0, len(si_if_list)):
            si_if = si_if_list[idx]
            static_routes = si_if.get_static_routes()
            if not static_routes:
                static_routes = {'route':[]}

            # update static routes
            try:
                domain_name, proj_name = si_obj.get_parent_fq_name()
                rt_name = si_obj.uuid + ' ' + str(idx)
                rt_fq_name = [domain_name, proj_name, rt_name]
                rt_obj = self._vnc_lib.interface_route_table_read(
                    fq_name=rt_fq_name)
                rt_obj.set_interface_route_table_routes(static_routes)
                self._vnc_lib.interface_route_table_update(rt_obj)
            except NoIdError:
                pass
    # end _update_static_routes

    def _create_svc_vm(self, vm_name, image_name, nics,
                       proj_name, flavor_name):
        n_client = self._novaclient_get(proj_name)
        if flavor_name:
            flavor = n_client.flavors.find(name=flavor_name)
        else:
            flavor = n_client.flavors.find(ram=4096)

        image = ''
        try:
            image = n_client.images.find(name=image_name)
        except nc_exc.NotFound:
            self._svc_syslog(
                "Error: Image %s not found in project %s"
                % (image_name, proj_name))
            return
        except nc_exc.NoUniqueMatch:
            self._svc_syslog(
                "Error: Multiple images %s found in project %s"
                % (image_name, proj_name))
            return

        # launch vm
        self._svc_syslog('Launching VM : ' + vm_name)
        nova_vm = n_client.servers.create(name=vm_name, image=image,
                                          flavor=flavor, nics=nics)
        nova_vm.get()
        self._svc_syslog('Created VM : ' + str(nova_vm))
        return nova_vm
    # end create_svc_vm

    def _create_svc_vn(self, vn_name, vn_subnet, proj_obj):
        self._svc_syslog(
            "Creating network %s subnet %s" % (vn_name, vn_subnet))

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
    # end _create_svc_vn

    def _cassandra_init(self):
        server_idx = 0
        num_dbnodes = len(self._args.cassandra_server_list)
        connected = False
        while not connected:
            try:
                cass_server = self._args.cassandra_server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                server_idx = (server_idx + 1) % num_dbnodes
                time.sleep(3)

        if self._args.reset_config:
            try:
                sys_mgr.drop_keyspace(SvcMonitor._KEYSPACE)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                print "Warning! " + str(e)

        try:
            sys_mgr.create_keyspace(SvcMonitor._KEYSPACE, SIMPLE_STRATEGY,
                                    {'replication_factor': str(num_dbnodes)})
        except pycassa.cassandra.ttypes.InvalidRequestException as e:
            print "Warning! " + str(e)

        column_families = [self._SVC_VM_CF,
                           self._SVC_CLEANUP_CF,
                           self._SVC_SI_CF]
        for cf in column_families:
            try:
                sys_mgr.create_column_family(SvcMonitor._KEYSPACE, cf)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                print "Warning! " + str(e)

        conn_pool = pycassa.ConnectionPool(SvcMonitor._KEYSPACE,
                                           self._args.cassandra_server_list)

        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        wr_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        self._svc_vm_cf = pycassa.ColumnFamily(conn_pool, self._SVC_VM_CF,
                                  read_consistency_level=rd_consistency,
                                  write_consistency_level=wr_consistency)
        self._svc_si_cf = pycassa.ColumnFamily(conn_pool, self._SVC_SI_CF,
                                  read_consistency_level=rd_consistency,
                                  write_consistency_level=wr_consistency)
        self._cleanup_cf = pycassa.ColumnFamily(conn_pool,
                                  self._SVC_CLEANUP_CF,
                                  read_consistency_level=rd_consistency,
                                  write_consistency_level=wr_consistency)
    # end _cassandra_init

# end class svc_monitor


def launch_arc(monitor, ssrc_mapc):
    arc_mapc = arc_initialize(monitor._args, ssrc_mapc)
    while True:
        # If not connected to zookeeper Pause the operations 
        if not _zookeeper_client.is_connected():
            time.sleep(1)
            continue
        try:
            pollreq = PollRequest(arc_mapc.get_session_id())
            result = arc_mapc.call('poll', pollreq)
            monitor.process_poll_result(result)
        except Exception as e:
            if type(e) == socket.error:
                time.sleep(3)
            else:
                cgitb_error_log(monitor)
#end launch_arc


def launch_ssrc(monitor):
    ssrc_mapc = ssrc_initialize(monitor._args)
    arc_glet = gevent.spawn(launch_arc, monitor, ssrc_mapc)
    arc_glet.join()
# end launch_ssrc


def timer_callback(monitor):
    # check health of VMs
    try:
        vm_list = list(monitor._svc_vm_cf.get_range())
    except Exception:
        return

    for vm_uuid, si in vm_list:
        try:
            proj_name = monitor._get_proj_name_from_si_fq_str(si['si_fq_str'])
            n_client = monitor._novaclient_get(proj_name)
            server = n_client.servers.find(id=vm_uuid)
        except nc_exc.NotFound:
            continue
        except Exception:
            continue
        else:
            if server.status == 'ERROR':
                monitor._restart_svc_vm(vm_uuid, si['si_fq_str'])
# end timer_callback


def launch_timer(monitor):
    while True:
        gevent.sleep(_CHECK_SVC_VM_HEALTH_INTERVAL)
        try:
            timer_callback(monitor)
        except Exception:
            cgitb_error_log(monitor)
#end launch_timer


def cleanup_callback(monitor):
    try:
        delete_list = list(monitor._cleanup_cf.get_range())
    except Exception:
        return

    for uuid, info in delete_list or []:
        if info['type'] == 'vm':
            monitor._delete_svc_instance_vm(uuid, info['proj_name'])
        elif info['type'] == 'vn':
            monitor._delete_shared_vn(uuid, info['proj_name'])
        elif info['type'] == 'iip':
            monitor._delete_shared_iip(uuid, info['proj_name'])
# end cleanup_callback


def launch_cleanup(monitor):
    while True:
        gevent.sleep(_CHECK_CLEANUP_INTERVAL)
        try:
            cleanup_callback(monitor)
        except Exception:
            cgitb_error_log(monitor)
#end launch_cleanup


def cgitb_error_log(monitor):
    cgitb.Hook(format="text",
               file=open(monitor._tmp_file, 'w')).handle(sys.exc_info())
    fhandle = open(monitor._tmp_file)
    monitor._svc_err_logger.error("%s" % fhandle.read())
#end cgitb_error_log


def parse_args(args_str):
    '''
    Eg. python svc_monitor.py --ifmap_server_ip 192.168.1.17
                         --ifmap_server_port 8443
                         --ifmap_username test
                         --ifmap_password test
                         --cassandra_server_list 10.1.2.3:9160
                         --api_server_ip 10.1.2.3
                         --api_server_port 8082
                         --zk_server_ip 10.1.2.3
                         --zk_server_port 2181
                         --collectors 127.0.0.1:8086
                         --disc_server_ip 127.0.0.1
                         --disc_server_port 5998
                         --http_server_port 8090
                         --log_local
                         --log_level SYS_DEBUG
                         --log_category test
                         --log_file <stdout>
                         [--region_name <name>]
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we show all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file",
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'ifmap_server_ip': '127.0.0.1',
        'ifmap_server_port': '8443',
        'ifmap_username': 'test2',
        'ifmap_password': 'test2',
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'zk_server_ip': '127.0.0.1',
        'zk_server_port': '2181',
        'collectors': None,
        'disc_server_ip': None,
        'disc_server_port': None,
        'http_server_port': '8088',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'region_name': None,
        }
    secopts = {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
        'ifmap_certauth_port': "8444",
    }
    ksopts = {
        'auth_host': '127.0.0.1',
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'default-domain'
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read([args.conf_file])
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
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

    parser.add_argument(
        "--ifmap_server_ip", help="IP address of ifmap server")
    parser.add_argument("--ifmap_server_port", help="Port of ifmap server")

    # TODO should be from certificate
    parser.add_argument("--ifmap_username",
                        help="Username known to ifmap server")
    parser.add_argument("--ifmap_password",
                        help="Password known to ifmap server")
    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--reset_config", action="store_true",
        help="Warning! Destroy previous configuration and start clean")
    parser.add_argument("--api_server_ip",
                        help="IP address of API server")
    parser.add_argument("--api_server_port",
                        help="Port of API server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
    parser.add_argument("--disc_server_ip",
                        help="IP address of the discovery server")
    parser.add_argument("--disc_server_port",
                        help="Port of the discovery server")
    parser.add_argument("--http_server_port",
                        help="Port of local HTTP server")
    parser.add_argument(
        "--log_local", action="store_true",
        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--admin_user",
                        help="Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help="Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help="Tenant name for keystone admin user")
    parser.add_argument("--region_name",
                        help="Region name for openstack API")
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    if args.region_name and args.region_name.lower() == 'none':
        args.region_name = None
    return args
# end parse_args


def run_svc_monitor(args=None):
    # Retry till API server is up
    connected = False
    while not connected:
        try:
            vnc_api = VncApi(
                args.admin_user, args.admin_password, args.admin_tenant_name,
                args.api_server_ip, args.api_server_port)
            connected = True
        except requests.exceptions.ConnectionError:
            time.sleep(3)
        except ResourceExhaustionError:  # haproxy throws 503
            time.sleep(3)
        except ResourceExhaustionError: # haproxy throws 503
            time.sleep(3)


    monitor = SvcMonitor(vnc_api, args)
    ssrc_task = gevent.spawn(launch_ssrc, monitor)
    timer_task = gevent.spawn(launch_timer, monitor)
    cleanup_task = gevent.spawn(launch_cleanup, monitor)
    gevent.joinall([ssrc_task, timer_task, cleanup_task])
# end run_svc_monitor


def main(args_str=None):
    global _zookeeper_client
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)

    _zookeeper_client = ZookeeperClient("svc-monitor", args.zk_server_ip)
    _zookeeper_client.master_election("/svc-monitor", os.getpid(),
                                  run_svc_monitor, args)
# end main

def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
