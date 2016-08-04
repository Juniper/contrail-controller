#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor to instantiate/scale/monitor services like firewall, LB, ...
"""

import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import gevent
from gevent import monkey
monkey.patch_all(thread=not 'unittest' in sys.modules)

from cfgm_common.zkclient import ZookeeperClient
import requests
import ConfigParser
import cgitb
import cStringIO
import argparse
import socket

import re
import os

import logging
import logging.handlers

from cfgm_common import exceptions
from cfgm_common.imid import *
from cfgm_common import importutils
from cfgm_common import svc_info
from cfgm_common import utils
from cfgm_common.utils import cgitb_hook

from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.vnc_cassandra import VncCassandraClient
from cfgm_common.vnc_db import DBBase
from config_db import *
from cfgm_common.dependency_tracker import DependencyTracker

from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.gen_py.process_info.ttypes import ConnectionType, \
    ConnectionStatus
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames

from vnc_api.vnc_api import *

import discoveryclient.client as client

from db import ServiceInstanceDB
from logger import ServiceMonitorLogger
from instance_manager import InstanceManager
from loadbalancer_agent import LoadbalancerAgent

from novaclient import exceptions as nc_exc

# zookeeper client connection
_zookeeper_client = None


class SvcMonitor(object):

    _DEFAULT_KS_CERT_BUNDLE="/tmp/keystonecertbundle.pem"
    _kscertbundle=None

    """
    data + methods used/referred to by ssrc and arc greenlets
    """
    _REACTION_MAP = {
        "service_appliance_set": {
            'self': [],
            'service_appliance': []
        },
        "service_appliance": {
            'self': ['service_appliance_set'],
            'service_appliance_set': []
        },
        "loadbalancer_pool": {
            'self': [],
            'virtual_ip': [],
            'loadbalancer_member': [],
            'loadbalancer_healthmonitor': [],
        },
        "loadbalancer_member": {
            'self': ['loadbalancer_pool'],
            'loadbalancer_pool': []
        },
        "virtual_ip": {
            'self': ['loadbalancer_pool'],
            'loadbalancer_pool': []
        },
        "loadbalancer_healthmonitor": {
            'self': ['loadbalancer_pool'],
            'loadbalancer_pool': []
        },
        "service_instance": {
            'self': ['virtual_machine'],
            'virtual_machine': []
        },
        "instance_ip": {
            'self': [],
        },
        "floating_ip": {
            'self': [],
        },
        "service_template": {
            'self': [],
        },
        "physical_router": {
            'self': [],
        },
        "physical_interface": {
            'self': [],
        },
        "logical_interface": {
            'self': [],
        },
        "virtual_network": {
            'self': [],
        },
        "virtual_machine": {
            'self': ['virtual_machine_interface'],
            'service_instance': [],
            'virtual_machine_interface': [],
        },
        "virtual_machine_interface": {
            'self': ['interface_route_table', 'virtual_machine'],
            'interface_route_table': [],
            'virtual_machine': [],
        },
        "interface_route_table": {
            'self': [],
            'virtual_machine_interface': [],
        },
        "project": {
            'self': [],
        },
    }

    def __init__(self, args=None):
        self._args = args

        # create database and logger
        self.si_db = ServiceInstanceDB(args)

        # initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(self._args.disc_server_ip,
                                                self._args.disc_server_port,
                                                ModuleNames[Module.SVC_MONITOR])

        # initialize logger
        self.logger = ServiceMonitorLogger(self.si_db, self._disc, args)
        self.si_db.add_logger(self.logger)
        self.si_db.init_database()

        # rotating log file for catchall errors
        self._err_file = self._args.trace_file
        self._svc_err_logger = logging.getLogger('SvcErrLogger')
        self._svc_err_logger.setLevel(logging.ERROR)
        try:
            with open(self._err_file, 'a'):
                handler = logging.handlers.RotatingFileHandler(
                    self._err_file, maxBytes=64*1024, backupCount=2)
                self._svc_err_logger.addHandler(handler)
        except IOError:
            self.logger.log_warning("Failed to open trace file %s" %
                self._err_file)

        # Connect to Rabbit and Initialize cassandra connection
        self._connect_rabbit()

    def _connect_rabbit(self):
        rabbit_server = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost
        rabbit_ha_mode = self._args.rabbit_ha_mode

        self._db_resync_done = gevent.event.Event()

        q_name = 'svc_mon.%s' % (socket.gethostname())
        self._vnc_kombu = VncKombuClient(rabbit_server, rabbit_port,
                                         rabbit_user, rabbit_password,
                                         rabbit_vhost, rabbit_ha_mode,
                                         q_name, self._vnc_subscribe_callback,
                                         self.config_log)

        cass_server_list = self._args.cassandra_server_list
        reset_config = self._args.reset_config
        self._cassandra = VncCassandraClient(cass_server_list, reset_config,
                                             self._args.cluster_id, None,
                                             self.config_log)
        DBBase.init(self, self.logger, self._cassandra)
    # end _connect_rabbit

    def config_log(self, msg, level):
        self.logger.log(msg, level)

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        try:
            self._vnc_subscribe_actions(oper_info)
        except Exception:
            cgitb_error_log(self)

    def _vnc_subscribe_actions(self, oper_info):
        try:
            msg = "Notification Message: %s" % (pformat(oper_info))
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            obj_type = oper_info['type'].replace('-', '_')
            obj_class = DBBase._OBJ_TYPE_MAP.get(obj_type)
            if obj_class is None:
                return

            if oper_info['oper'] == 'CREATE' or oper_info['oper'] == 'UPDATE':
                dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP,
                    self._REACTION_MAP)
                obj_id = oper_info['uuid']
                obj = obj_class.get(obj_id)
                if obj is not None:
                    dependency_tracker.evaluate(obj_type, obj)
                else:
                    obj = obj_class.locate(obj_id)
                obj.update()
                dependency_tracker.evaluate(obj_type, obj)
            elif oper_info['oper'] == 'DELETE':
                obj_id = oper_info['uuid']
                obj = obj_class.get(obj_id)
                if obj is None:
                    return
                dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP,
                    self._REACTION_MAP)
                dependency_tracker.evaluate(obj_type, obj)
                obj_class.delete(obj_id)
            else:
                # unknown operation
                self.config_log('Unknown operation %s' % oper_info['oper'],
                                level=SandeshLevel.SYS_ERR)
                return

            if obj is None:
                self.config_log('Error while accessing %s uuid %s' % (
                                obj_type, obj_id))
                return

        except Exception:
            cgitb_error_log(self)

        for sas_id in dependency_tracker.resources.get('service_appliance_set', []):
            sas_obj = ServiceApplianceSetSM.get(sas_id)
            if sas_obj is not None:
                sas_obj.add()

        for lb_pool_id in dependency_tracker.resources.get('loadbalancer_pool', []):
            lb_pool = LoadbalancerPoolSM.get(lb_pool_id)
            if lb_pool is not None:
                lb_pool.add()

        for si_id in dependency_tracker.resources.get('service_instance', []):
            si = ServiceInstanceSM.get(si_id)
            if si:
                si.state = 'launch'
                self._create_service_instance(si)
            else:
                self.logger.log_info("Deleting SI %s" % si_id)
                for vm_id in dependency_tracker.resources.get(
                        'virtual_machine', []):
                    vm = VirtualMachineSM.get(vm_id)
                    self._delete_service_instance(vm)
                self.logger.log_info("SI %s deletion succeed" % si_id)

        for vn_id in dependency_tracker.resources.get('virtual_network', []):
            vn = VirtualNetworkSM.get(vn_id)
            if vn:
                for si_id in ServiceInstanceSM:
                    si = ServiceInstanceSM.get(si_id)
                    intf_list = []
                    if si.params:
                        intf_list = si.params.get('interface_list', [])
                    for intf in intf_list:
                        if (':').join(vn.fq_name) in intf.values():
                            self._create_service_instance(si)

        for vmi_id in dependency_tracker.resources.get('virtual_machine_interface', []):
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            if vmi:
                # If it's a VIP port and security_group changed,
                # we need to update the corresponding SI VM ports
                self.update_sg_si_vmi(vmi)

                for vm_id in dependency_tracker.resources.get(
                        'virtual_machine', []):
                    vm = VirtualMachineSM.get(vm_id)
                    if vm:
                        self.check_link_si_to_vm(vm, vmi)
            else:
                for irt_id in dependency_tracker.resources.get(
                        'interface_route_table', []):
                    self._delete_interface_route_table(irt_id)

        for fip_id in dependency_tracker.resources.get('floating_ip', []):
            fip = FloatingIpSM.get(fip_id)
            if fip:
                for vmi_id in fip.virtual_machine_interfaces:
                    vmi = VirtualMachineInterfaceSM.get(vmi_id)
                    if vmi and vmi.virtual_ip:
                        self.netns_manager.add_fip_to_vip_vmi(vmi, fip)

    def post_init(self, vnc_lib, args=None):
        # api server
        self._vnc_lib = vnc_lib

        self._nova_client = importutils.import_object(
            'svc_monitor.nova_client.ServiceMonitorNovaClient',
            self._args, self.logger, SvcMonitor._kscertbundle)

        # load vrouter scheduler
        self.vrouter_scheduler = importutils.import_object(
            self._args.si_netns_scheduler_driver,
            self._vnc_lib, self._nova_client,
            self._disc, self.logger, self._args)

        # load virtual machine instance manager
        self.vm_manager = importutils.import_object(
            'svc_monitor.virtual_machine_manager.VirtualMachineManager',
            self._vnc_lib, self.si_db, self.logger,
            self.vrouter_scheduler, self._nova_client, self._args)

        # load network namespace instance manager
        self.netns_manager = importutils.import_object(
            'svc_monitor.instance_manager.NetworkNamespaceManager',
            self._vnc_lib, self.si_db, self.logger,
            self.vrouter_scheduler, self._nova_client, self._args)

        # load a vrouter instance manager
        self.vrouter_manager = importutils.import_object(
            'svc_monitor.vrouter_instance_manager.VRouterInstanceManager',
            self._vnc_lib, self.si_db, self.logger,
            self.vrouter_scheduler, self._nova_client, self._args)

        # load a loadbalancer agent
        self.loadbalancer_agent = LoadbalancerAgent(self, self._vnc_lib, self._args)

        # Read the cassandra and populate the entry in ServiceMonitor DB
        self.sync_sm()

        # create default analyzer template
        self._create_default_template('analyzer-template', 'analyzer',
                                      flavor='m1.medium',
                                      image_name='analyzer')
        # create default NAT template
        self._create_default_template('nat-template', 'firewall',
                                      svc_mode='in-network-nat',
                                      image_name='analyzer',
                                      flavor='m1.medium')
        # create default netns SNAT template
        self._create_default_template('netns-snat-template', 'source-nat',
                                      svc_mode='in-network-nat',
                                      hypervisor_type='network-namespace',
                                      scaling=True)
        # create default loadbalancer template
        self._create_default_template('haproxy-loadbalancer-template', 'loadbalancer',
                                      svc_mode='in-network-nat',
                                      hypervisor_type='network-namespace',
                                      scaling=True)
        self._create_default_template('docker-template', 'firewall',
                                      svc_mode='transparent',
                                      image_name="ubuntu",
                                      hypervisor_type='vrouter-instance',
                                      vrouter_instance_type='docker',
                                      instance_data={
                                          "command": "/bin/bash"
                                      })

        # upgrade handling
        self.upgrade()

        # check services
        self.vrouter_scheduler.vrouters_running()
        self.launch_services()

        self._db_resync_done.set()

    def upgrade(self):
        for si in ServiceInstanceSM.values():
            st = ServiceTemplateSM.get(si.service_template)
            if not st:
                continue

            vm_id_list = list(si.virtual_machines)
            for vm_id in vm_id_list:
                vm = VirtualMachineSM.get(vm_id)
                if vm.virtualization_type:
                    continue

                try:
                    nova_vm = self._nova_client.oper('servers', 'get',
                        si.proj_name, id=vm_id)
                except nc_exc.NotFound:
                    nova_vm = None

                if nova_vm:
                    vm_name = nova_vm.name
                    vm.proj_fq_name = nova_vm.name.split('__')[0:2]
                else:
                    vm_name = vm.name

                if not vm_name.split('__')[-1].isdigit():
                    continue

                vm.virtualization_type = st.virtualization_type
                self._delete_service_instance(vm)

    def launch_services(self):
        for si in ServiceInstanceSM.values():
            self._create_service_instance(si)

    def sync_sm(self):
        vn_set = set()
        vmi_set = set()
        iip_set = set()
        ok, lb_pool_list = self._cassandra._cassandra_loadbalancer_pool_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in lb_pool_list:
                try:
                    lb_pool = LoadbalancerPoolSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for lb pool %s" % uuid)
                    continue
                if lb_pool.virtual_machine_interface:
                    vmi_set.add(lb_pool.virtual_machine_interface)

        ok, lb_pool_member_list = self._cassandra._cassandra_loadbalancer_member_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in lb_pool_member_list:
                try:
                    lb_pool_member = LoadbalancerMemberSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for lb member %s" % uuid)
                    continue

        ok, lb_vip_list = self._cassandra._cassandra_virtual_ip_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in lb_vip_list:
                try:
                    virtual_ip = VirtualIpSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for lb vip %s" % uuid)
                    continue
                if virtual_ip.virtual_machine_interface:
                    vmi_set.add(virtual_ip.virtual_machine_interface)

        ok, lb_hm_list = self._cassandra._cassandra_loadbalancer_healthmonitor_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in lb_hm_list:
                try:
                    lb_hm = HealthMonitorSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for lb healthmonitor %s" % uuid)
                    continue

        ok, si_list = self._cassandra._cassandra_service_instance_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in si_list:
                try:
                    si = ServiceInstanceSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for service instance %s" % uuid)
                    continue

        ok, st_list = self._cassandra._cassandra_service_template_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in st_list:
                try:
                    st = ServiceTemplateSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for service template %s" % uuid)
                    continue

        ok, vn_list = self._cassandra._cassandra_virtual_network_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in vn_list:
                try:
                    vn = VirtualNetworkSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for virtual network %s" % uuid)
                    continue
                vmi_set |= vn.virtual_machine_interfaces

        ok, ifd_list = self._cassandra._cassandra_physical_interface_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in ifd_list:
                try:
                    ifd = PhysicalInterfaceSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for physical interface %s" % uuid)
                    continue


        ok, ifl_list = self._cassandra._cassandra_logical_interface_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in ifl_list:
                try:
                    ifl = LogicalInterfaceSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for logical interface %s" % uuid)
                    continue
                if ifl.virtual_machine_interface:
                    vmi_set.add(ifl.virtual_machine_interface)

        ok, pr_list = self._cassandra._cassandra_physical_router_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in pr_list:
                try:
                    pr = PhysicalRouterSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for physical router %s" % uuid)
                    continue

        ok, vr_list = self._cassandra._cassandra_virtual_router_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in vr_list:
                try:
                    vr = VirtualRouterSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for virtual router %s" % uuid)
                    continue

        ok, vmi_list = self._cassandra._cassandra_virtual_machine_interface_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in vmi_list:
                try:
                    vmi = VirtualMachineInterfaceSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for virtual machine interface %s" % uuid)
                    continue
                if vmi.instance_ip:
                    iip_set.add(vmi.instance_ip)

        ok, irt_list = self._cassandra._cassandra_interface_route_table_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in irt_list:
                try:
                    irt = InterfaceRouteTableSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for interface route table %s" % uuid)
                    continue

        ok, project_list = self._cassandra._cassandra_project_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in project_list:
                try:
                    prj = ProjectSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for project %s" % uuid)
                    continue

        ok, sas_list = self._cassandra._cassandra_service_appliance_set_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in sas_list:
                try:
                    sas = ServiceApplianceSetSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for service appliance set %s" % uuid)
                    continue

        ok, sa_list = self._cassandra._cassandra_service_appliance_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in sa_list:
                try:
                    sa = ServiceApplianceSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for service appliance %s" % uuid)
                    continue

        ok, domain_list = self._cassandra._cassandra_domain_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in domain_list:
                try:
                    DomainSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for domain %s" % uuid)
                    continue

        ok, iip_list = self._cassandra._cassandra_instance_ip_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in iip_list:
                try:
                    InstanceIpSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for instance ip %s" % uuid)
                    continue

        ok, fip_list = self._cassandra._cassandra_floating_ip_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in fip_list:
                try:
                    FloatingIpSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for floating ip %s" % uuid)
                    continue

        ok, sg_list = self._cassandra._cassandra_security_group_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in sg_list:
                try:
                    SecurityGroupSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for security group %s" % uuid)
                    continue

        ok, vm_list = self._cassandra._cassandra_virtual_machine_list()
        if not ok:
            pass
        else:
            for fq_name, uuid in vm_list:
                try:
                    vm = VirtualMachineSM.locate(uuid)
                except NoIdError:
                    self.logger.log_error("db entry missing for virtual machine %s" % uuid)
                    continue
                except Exception:
                    self.logger.log_error("db entry corrupt for virtual machine %s" % uuid)
                    continue
                if vm.service_instance:
                    continue
                for vmi_id in vm.virtual_machine_interfaces:
                    vmi = VirtualMachineInterfaceSM.get(vmi_id)
                    if not vmi:
                        continue
                    self.check_link_si_to_vm(vm, vmi)

        # Load the loadbalancer driver
        self.loadbalancer_agent.load_drivers()

        for lb_pool in LoadbalancerPoolSM.values():
            lb_pool.add()

        # Audit the lb pools
        self.loadbalancer_agent.audit_lb_pools()

    # end sync_sm

    # create service template
    def _create_default_template(self, st_name, svc_type, svc_mode=None,
                                 hypervisor_type='virtual-machine',
                                 image_name=None, flavor=None, scaling=False,
                                 vrouter_instance_type=None,
                                 instance_data=None):
        domain_name = 'default-domain'
        domain_fq_name = [domain_name]
        st_fq_name = [domain_name, st_name]
        self.logger.log_info("Creating %s %s hypervisor %s" %
            (domain_name, st_name, hypervisor_type))

        domain_obj = None
        for domain in DomainSM.values():
            if domain.fq_name == domain_fq_name:
                domain_obj = Domain()
                domain_obj.uuid = domain.uuid
                domain_obj.fq_name = domain_fq_name
                break
        if not domain_obj:
            self.logger.log_error("%s domain not found" % (domain_name))
            return

        for st in ServiceTemplateSM.values():
            if st.fq_name == st_fq_name:
                self.logger.log_info("%s exists uuid %s" %
                    (st.name, str(st.uuid)))
                return

        svc_properties = ServiceTemplateType()
        svc_properties.set_service_type(svc_type)
        svc_properties.set_service_mode(svc_mode)
        svc_properties.set_service_virtualization_type(hypervisor_type)
        svc_properties.set_image_name(image_name)
        svc_properties.set_flavor(flavor)
        svc_properties.set_ordered_interfaces(True)
        svc_properties.set_service_scaling(scaling)

        # set interface list
        if svc_type == 'analyzer':
            if_list = [['left', False]]
        elif hypervisor_type == 'network-namespace':
            if_list = [['right', True], ['left', True]]
        else:
            if_list = [
                ['management', False], ['left', False], ['right', False]]

        for itf in if_list:
            if_type = ServiceTemplateInterfaceType(shared_ip=itf[1])
            if_type.set_service_interface_type(itf[0])
            svc_properties.add_interface_type(if_type)

        if vrouter_instance_type is not None:
            svc_properties.set_vrouter_instance_type(vrouter_instance_type)

        if instance_data is not None:
            svc_properties.set_instance_data(
                json.dumps(instance_data, separators=(',', ':')))

        st_obj = ServiceTemplate(name=st_name, domain_obj=domain)
        st_obj.set_service_template_properties(svc_properties)
        try:
            st_uuid = self._vnc_lib.service_template_create(st_obj)
        except Exception as e:
            self.logger.log_error("%s create failed with error %s" %
                (st_name, str(e)))
            return

        # Create the service template in local db
        ServiceTemplateSM.locate(st_uuid)

        self.logger.log_info("%s created with uuid %s" %
            (st_name, str(st_uuid)))
    #_create_default_analyzer_template

    def check_link_si_to_vm(self, vm, vmi):
        if vm.service_instance:
            return
        if not vmi.if_type:
            return

        si_fq_name = vmi.name.split('__')[0:3]
        index = int(vmi.name.split('__')[3]) - 1
        for si in ServiceInstanceSM.values():
            if si.fq_name != si_fq_name:
                continue
            st = ServiceTemplateSM.get(si.service_template)
            self.vm_manager.link_si_to_vm(si, st, index, vm.uuid)
            return

    def _create_service_instance(self, si):
        if si.state == 'active':
            return
        st = ServiceTemplateSM.get(si.service_template)
        if not st:
            self.logger.log_error("template not found for %s" %
                ((':').join(si.fq_name)))
            return

        self.logger.log_info("Creating SI %s (%s)" %
                             ((':').join(si.fq_name), st.virtualization_type))
        try:
            if st.virtualization_type == 'virtual-machine':
                self.vm_manager.create_service(st, si)
            elif st.virtualization_type == 'network-namespace':
                self.netns_manager.create_service(st, si)
            elif st.virtualization_type == 'vrouter-instance':
                self.vrouter_manager.create_service(st, si)
            else:
                self.logger.log_error("Unknown virt type: %s" %
                    st.virtualization_type)
        except Exception:
            cgitb_error_log(self)
        si.launch_count += 1
        self.logger.log_info("SI %s creation success" % (':').join(si.fq_name))

    def _delete_service_instance(self, vm):
        self.logger.log_info("Deleting VM %s %s" %
            ((':').join(vm.fq_name), vm.uuid))

        try:
            if vm.virtualization_type == svc_info.get_vm_instance_type():
                self.vm_manager.delete_service(vm)
            elif vm.virtualization_type == svc_info.get_netns_instance_type():
                self.netns_manager.delete_service(vm)
            elif vm.virtualization_type == 'vrouter-instance':
                self.vrouter_manager.delete_service(vm)
        except Exception:
            cgitb_error_log(self)

        # generate UVE
        si_fq_name = vm.display_name.split('__')[:-2]
        si_fq_str = (':').join(si_fq_name)
        self.logger.uve_svc_instance(si_fq_str, status='DELETE',
                                     vms=[{'uuid': vm.uuid}])
        return True

    def _relaunch_service_instance(self, si):
        si.state = 'relaunch'
        self._create_service_instance(si)

    def _check_service_running(self, si):
        st = ServiceTemplateSM.get(si.service_template)
        if st.virtualization_type == 'virtual-machine':
            status = self.vm_manager.check_service(si)
        elif st.virtualization_type == 'network-namespace':
            status = self.netns_manager.check_service(si)
        elif st.virtualization_type == 'vrouter-instance':
            status = self.vrouter_manager.check_service(si)

        return status

    def _delete_interface_route_table(self, irt_uuid):
        try:
            self._vnc_lib.interface_route_table_delete(id=irt_uuid)
            InterfaceRouteTableSM.delete(irt_uuid)
        except (NoIdError, RefsExistError):
            return

    def _delete_shared_vn(self, vn_uuid):
        try:
            self.logger.log_info("Deleting vn %s" % (vn_uuid))
            self._vnc_lib.virtual_network_delete(id=vn_uuid)
            VirtualNetworkSM.delete(vn_uuid)
        except (NoIdError, RefsExistError):
            pass

    @staticmethod
    def reset():
        for cls in DBBase._OBJ_TYPE_MAP.values():
            cls.reset()

    def update_sg_si_vmi(self, vip_sm):
        if vip_sm.instance_ip is None:
            return
        vip_obj = self._vnc_lib.virtual_machine_interface_read(id=vip_sm.uuid)
        instance_ip = InstanceIpSM.get(vip_sm.instance_ip)
        for vmi_uuid in instance_ip.virtual_machine_interfaces:
            # Skip the same VIP port
            if vmi_uuid == vip_obj.uuid:
                continue
            else:
                vmi_sm = VirtualMachineInterfaceSM.get(vmi_uuid)
                vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)

                if vmi_obj.get_security_group_refs() == vip_obj.get_security_group_refs():
                    continue

                # Delete old security group refs
                for sec_group in vmi_obj.get_security_group_refs() or []:
                    self._vnc_lib.ref_update('virtual_machine_interface', vmi_uuid,
                                             'security_group', sec_group['uuid'],
                                             None, 'DELETE')

                # Create new security group refs
                for sec_group in vip_obj.get_security_group_refs() or []:
                    self._vnc_lib.ref_update('virtual_machine_interface', vmi_uuid,
                                             'security_group', sec_group['uuid'],
                                             None, 'ADD')

                vmi_sm.update()


def skip_check_service(si):
    # wait for first launch
    if not si.launch_count:
        return True
    # back off going on
    if si.back_off > 0:
        si.back_off -= 1
        return True
    # back off done
    if si.back_off == 0:
        si.back_off = -1
        return False
    # set back off
    if not si.launch_count % 10:
        si.back_off = 10
        return True
    return False

def timer_callback(monitor):
    # delete vms without si
    vm_delete_list = []
    for vm in VirtualMachineSM.values():
        si = ServiceInstanceSM.get(vm.service_instance)
        if not si and vm.virtualization_type:
            vm_delete_list.append(vm)
    for vm in vm_delete_list:
        monitor._delete_service_instance(vm)

    monitor.vrouter_scheduler.vrouters_running()

    # check status of service
    si_list = list(ServiceInstanceSM.values())
    for si in si_list:
        if skip_check_service(si):
            continue
        if not monitor._check_service_running(si):
            monitor._relaunch_service_instance(si)
        if si.max_instances != len(si.virtual_machines):
            monitor._relaunch_service_instance(si)

    # check vns to be deleted
    for project in ProjectSM.values():
        if project.service_instances:
            continue

        vn_id_list = list(project.virtual_networks)
        for vn_id in vn_id_list:
            vn = VirtualNetworkSM.get(vn_id)
            if not vn or vn.virtual_machine_interfaces:
                continue
            if vn.name in svc_info.get_shared_vn_list():
                monitor._delete_shared_vn(vn.uuid)
            elif vn.name.startswith(svc_info.get_snat_left_vn_prefix()):
                monitor._delete_shared_vn(vn.uuid)

def launch_timer(monitor):
    if not monitor._args.check_service_interval.isdigit():
        monitor.logger.log_emergency("set seconds for check_service_interval "
            "in contrail-svc-monitor.conf. example: check_service_interval=60")
        sys.exit()
    monitor.logger.log_notice("check_service_interval set to %s seconds" %
        monitor._args.check_service_interval)

    while True:
        gevent.sleep(int(monitor._args.check_service_interval))
        try:
            timer_callback(monitor)
        except Exception:
            cgitb_error_log(monitor)

def cgitb_error_log(monitor):
    string_buf = cStringIO.StringIO()
    cgitb_hook(file=string_buf, format="text")
    monitor.config_log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)

def parse_args(args_str):
    '''
    Eg. python svc_monitor.py --ifmap_server_ip 192.168.1.17
                         --ifmap_server_port 8443
                         --ifmap_username test
                         --ifmap_password test
                         --rabbit_server localhost
                         --rabbit_port 5672
                         --rabbit_user guest
                         --rabbit_password guest
                         --cassandra_server_list 10.1.2.3:9160
                         --api_server_ip 10.1.2.3
                         --api_server_port 8082
                         --api_server_use_ssl False
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
                         --trace_file /var/log/contrail/svc-monitor.err
                         --use_syslog
                         --syslog_facility LOG_USER
                         --cluster_id <testbed-name>
                         --check_service_interval 60
                         [--region_name <name>]
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we show all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'ifmap_server_ip': '127.0.0.1',
        'ifmap_server_port': '8443',
        'ifmap_username': 'test2',
        'ifmap_password': 'test2',
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'api_server_use_ssl': False,
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
        'trace_file': '/var/log/contrail/svc-monitor.err',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'region_name': None,
        'cluster_id': '',
        'check_service_interval': '60',
        'sandesh_send_rate_limit' : SandeshSystem.get_sandesh_send_rate_limit(),
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
        'auth_protocol': 'http',
        'auth_port': '5000',
        'auth_version': 'v2.0',
        'auth_insecure': True,
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'default-domain',
        'certfile': '',
        'keyfile': '',
        'cafile': '',
    }
    schedops = {
        'si_netns_scheduler_driver': \
            'svc_monitor.scheduler.vrouter_scheduler.RandomScheduler',
        'analytics_server_ip': '127.0.0.1',
        'analytics_server_port': '8081',
        'availability_zone': None,
        'netns_availability_zone': None,
    }

    config = ConfigParser.SafeConfigParser()
    if args.conf_file:
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))
        if 'SCHEDULER' in config.sections():
            schedops.update(dict(config.items("SCHEDULER")))

    kscertfile=ksopts.get('certfile')
    kskeyfile=ksopts.get('keyfile')
    kscafile=ksopts.get('cafile')
    ksauthproto=ksopts.get('auth_protocol')
    if kscertfile and kskeyfile and kscafile \
    and ksauthproto == 'https':
        certs=[kscertfile, kskeyfile, kscafile]
        SvcMonitor._kscertbundle=utils.getCertKeyCaBundle(SvcMonitor._DEFAULT_KS_CERT_BUNDLE,certs)
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
    defaults.update(schedops)
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--ifmap_server_ip", help="IP address of ifmap server")
    parser.add_argument("--ifmap_server_port", help="Port of ifmap server")

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
    parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
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
    parser.add_argument("--trace_file", help="Filename for the error "
                        "backtraces to be written to")
    parser.add_argument("--use_syslog", action="store_true",
                        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
                        help="Syslog facility to receive log lines")
    parser.add_argument("--admin_user",
                        help="Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help="Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help="Tenant name for keystone admin user")
    parser.add_argument("--region_name",
                        help="Region name for openstack API")
    parser.add_argument("--cluster_id",
                        help="Used for database keyspace separation")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec.")

    args = parser.parse_args(remaining_argv)
    args.config_sections = config
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    if args.region_name and args.region_name.lower() == 'none':
        args.region_name = None
    if args.availability_zone and args.availability_zone.lower() == 'none':
        args.availability_zone = None
    if args.netns_availability_zone and \
            args.netns_availability_zone.lower() == 'none':
        args.netns_availability_zone = None
    return args


def run_svc_monitor(args=None):
    monitor = SvcMonitor(args)

    monitor._zookeeper_client = _zookeeper_client

    # Retry till API server is up
    connected = False
    monitor.logger.api_conn_status_update(ConnectionStatus.INIT)

    while not connected:
        try:
            vnc_api = VncApi(
                args.admin_user, args.admin_password, args.admin_tenant_name,
                args.api_server_ip, args.api_server_port,
                api_server_use_ssl=args.api_server_use_ssl)
            connected = True
            monitor.logger.api_conn_status_update(ConnectionStatus.UP)
        except requests.exceptions.ConnectionError as e:
            monitor.logger.api_conn_status_update(ConnectionStatus.DOWN, str(e))
            time.sleep(3)
        except ResourceExhaustionError:  # haproxy throws 503
            time.sleep(3)
        except ResourceExhaustionError: # haproxy throws 503
            time.sleep(3)

    monitor.post_init(vnc_api, args)
    timer_task = gevent.spawn(launch_timer, monitor)
    gevent.joinall([timer_task])


def main(args_str=None):
    global _zookeeper_client
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    if args.cluster_id:
        client_pfx = args.cluster_id + '-'
        zk_path_pfx = args.cluster_id + '/'
    else:
        client_pfx = ''
        zk_path_pfx = ''

    _zookeeper_client = ZookeeperClient(client_pfx+"svc-monitor", args.zk_server_ip)
    _zookeeper_client.master_election(zk_path_pfx+"/svc-monitor", os.getpid(),
                                  run_svc_monitor, args)
# end main

def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
