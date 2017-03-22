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
import cStringIO
import argparse
import signal
import random
import hashlib

import os

import logging
import logging.handlers

from cfgm_common import importutils
from cfgm_common import svc_info
from cfgm_common import vnc_cgitb
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_amqp import VncAmqpHandle

from config_db import *

from pysandesh.sandesh_base import Sandesh, SandeshSystem, SandeshConfig
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from sandesh_common.vns.ttypes import Module

from vnc_api.vnc_api import *

from agent_manager import AgentManager
from db import ServiceMonitorDB
from logger import ServiceMonitorLogger
from module_logger import ServiceMonitorModuleLogger
from loadbalancer_agent import LoadbalancerAgent
from port_tuple import PortTupleAgent
from snat_agent import SNATAgent
from reaction_map import REACTION_MAP

try:
    from novaclient import exceptions as nc_exc
except ImportError:
    pass

# zookeeper client connection
_zookeeper_client = None


class SvcMonitor(object):

    def __init__(self, sm_logger=None, args=None):
        self._args = args
        # initialize logger
        if sm_logger is not None:
            self.logger = sm_logger
        else:
            # Initialize logger
            self.logger = ServiceMonitorLogger(args)

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
            self.logger.warning("Failed to open trace file %s" %
                                    self._err_file)

        # init object_db
        self._object_db = ServiceMonitorDB(self._args, self.logger)
        DBBaseSM.init(self, self.logger, self._object_db)

        # init rabbit connection
        self.rabbit = VncAmqpHandle(self.logger, DBBaseSM,
                REACTION_MAP, 'svc_monitor', args=self._args)
        self.rabbit.establish()

    def post_init(self, vnc_lib, args=None):
        # api server
        self._vnc_lib = vnc_lib

        try:
            self._nova_client = importutils.import_object(
                'svc_monitor.nova_client.ServiceMonitorNovaClient',
                self._args, self.logger)
        except Exception as e:
            self._nova_client = None

        # agent manager
        self._agent_manager = AgentManager()

        # load vrouter scheduler
        self.vrouter_scheduler = importutils.import_object(
            self._args.si_netns_scheduler_driver,
            self._vnc_lib, self._nova_client,
            None, self.logger, self._args)

        # load virtual machine instance manager
        self.vm_manager = importutils.import_object(
            'svc_monitor.virtual_machine_manager.VirtualMachineManager',
            self._vnc_lib, self._object_db, self.logger,
            self.vrouter_scheduler, self._nova_client, self._agent_manager,
            self._args)

        # load network namespace instance manager
        self.netns_manager = importutils.import_object(
            'svc_monitor.instance_manager.NetworkNamespaceManager',
            self._vnc_lib, self._object_db, self.logger,
            self.vrouter_scheduler, self._nova_client, self._agent_manager,
            self._args)

        # load a vrouter instance manager
        self.vrouter_manager = importutils.import_object(
            'svc_monitor.vrouter_instance_manager.VRouterInstanceManager',
            self._vnc_lib, self._object_db, self.logger,
            self.vrouter_scheduler, self._nova_client,
            self._agent_manager, self._args)

        # load PNF instance manager
        self.ps_manager = importutils.import_object(
            'svc_monitor.physical_service_manager.PhysicalServiceManager',
            self._vnc_lib, self._object_db, self.logger,
            self.vrouter_scheduler, self._nova_client,
            self._agent_manager, self._args)

        # load a loadbalancer agent
        self.loadbalancer_agent = LoadbalancerAgent(
            self, self._vnc_lib,
            self._object_db, self._args)
        self._agent_manager.register_agent(self.loadbalancer_agent)

        # load a snat agent
        self.snat_agent = SNATAgent(self, self._vnc_lib,
                                    self._object_db, self._args,
                                    ServiceMonitorModuleLogger(self.logger))
        self._agent_manager.register_agent(self.snat_agent)

        # load port tuple agent
        self.port_tuple_agent = PortTupleAgent(self, self._vnc_lib,
            self._object_db, self._args, ServiceMonitorModuleLogger(self.logger))
        self._agent_manager.register_agent(self.port_tuple_agent)

        # Read the object_db and populate the entry in ServiceMonitor DB
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
        self._create_default_template('haproxy-loadbalancer-template',
                                      'loadbalancer',
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

        self.rabbit._db_resync_done.set()

    def _upgrade_instance_ip(self, vm):
        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            if not vmi:
                continue

            for iip_id in vmi.instance_ips:
                iip = InstanceIpSM.get(iip_id)
                if not iip or iip.service_instance_ip:
                    continue
                iip_obj = InstanceIp()
                iip_obj.name = iip.name
                iip_obj.uuid = iip.uuid
                iip_obj.set_service_instance_ip(True)
                try:
                    self._vnc_lib.instance_ip_update(iip_obj)
                except NoIdError:
                    self.logger.error("upgrade instance ip to service ip failed %s" % (iip.name))
                    continue

    def _upgrade_auto_policy(self, si, st):
        if st.name != 'netns-snat-template':
            return
        if not si.params['auto_policy']:
            return

        si_obj = ServiceInstance()
        si_obj.uuid = si.uuid
        si_obj.fq_name = si.fq_name
        si_props = ServiceInstanceType(**si.params)
        si_props.set_auto_policy(False)
        si_obj.set_service_instance_properties(si_props)
        try:
            self._vnc_lib.service_instance_update(si_obj)
            self.logger.notice("snat policy upgraded for %s" % (si.name))
        except NoIdError:
            self.logger.error("snat policy upgrade failed for %s" % (si.name))
            return

    def upgrade(self):
        for lr in LogicalRouterSM.values():
            self.snat_agent.upgrade(lr)

        for si in ServiceInstanceSM.values():
            st = ServiceTemplateSM.get(si.service_template)
            if not st:
                continue

            self._upgrade_auto_policy(si, st)

            vm_id_list = list(si.virtual_machines)
            for vm_id in vm_id_list:
                vm = VirtualMachineSM.get(vm_id)
                self._upgrade_instance_ip(vm)
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
                self.delete_service_instance(vm)

    def launch_services(self):
        for si in ServiceInstanceSM.values():
            self.create_service_instance(si)

    def sync_sm(self):
        # Read and Sync all DBase
        for cls in DBBaseSM.get_obj_type_map().values():
            for obj in cls.list_obj():
                cls.locate(obj['uuid'], obj)

        # Link SI and VM
        for vm in VirtualMachineSM.values():
            if vm.service_instance:
                continue
            for vmi_id in vm.virtual_machine_interfaces:
                vmi = VirtualMachineInterfaceSM.get(vmi_id)
                if not vmi:
                    continue
                self.port_delete_or_si_link(vm, vmi)

        # invoke port tuple handling
        try:
            self.port_tuple_agent.update_port_tuples()
        except Exception:
            cgitb_error_log(self)

        # Load the loadbalancer driver
        self.loadbalancer_agent.load_drivers()

        # Invoke the health monitors
        for hm in HealthMonitorSM.values():
            hm.sync()

        # Invoke the loadbalancers
        for lb in LoadbalancerSM.values():
            lb.sync()

        # Invoke the loadbalancer listeners
        for lb_listener in LoadbalancerListenerSM.values():
            lb_listener.sync()

        # Invoke the loadbalancer pools
        for lb_pool in LoadbalancerPoolSM.values():
            lb_pool.sync()

        # Audit the lb pools
        self.loadbalancer_agent.audit_lb_pools()

        # Audit the SNAT instances
        self.snat_agent.audit_snat_instances()
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
        self.logger.info("Creating %s %s hypervisor %s" %
                             (domain_name, st_name, hypervisor_type))

        domain_obj = None
        for domain in DomainSM.values():
            if domain.fq_name == domain_fq_name:
                domain_obj = Domain()
                domain_obj.uuid = domain.uuid
                domain_obj.fq_name = domain_fq_name
                break
        if not domain_obj:
            self.logger.error("%s domain not found" % (domain_name))
            return

        for st in ServiceTemplateSM.values():
            if st.fq_name == st_fq_name:
                self.logger.info("%s exists uuid %s" %
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
            self.logger.error("%s create failed with error %s" %
                                  (st_name, str(e)))
            return

        # Create the service template in local db
        ServiceTemplateSM.locate(st_uuid)

        self.logger.info("%s created with uuid %s" %
                             (st_name, str(st_uuid)))
    #_create_default_analyzer_template

    def port_delete_or_si_link(self, vm, vmi):
        if vmi.port_tuple:
            return
        if (vmi.service_instance and vmi.virtual_machine == None):
            self.vm_manager.cleanup_svc_vm_ports([vmi.uuid])
            return

        if not vm or vm.service_instance:
            return
        if not vmi.if_type:
            return

        if len(vmi.name.split('__')) < 4:
            return
        si_fq_name = vmi.name.split('__')[0:3]
        index = int(vmi.name.split('__')[3]) - 1
        for si in ServiceInstanceSM.values():
            if si.fq_name != si_fq_name:
                continue
            st = ServiceTemplateSM.get(si.service_template)
            self.vm_manager.link_si_to_vm(si, st, index, vm.uuid)
            return

    def create_service_instance(self, si):
        if si.state == 'active':
            return
        st = ServiceTemplateSM.get(si.service_template)
        if not st:
            self.logger.error("template not found for %s" %
                                  ((':').join(si.fq_name)))
            return
        if st.params and st.params.get('version', 1) == 2:
            return

        self.logger.info("Creating SI %s (%s)" %
                             ((':').join(si.fq_name), st.virtualization_type))
        try:
            if st.virtualization_type == 'virtual-machine':
                self.vm_manager.create_service(st, si)
            elif st.virtualization_type == 'network-namespace':
                self.netns_manager.create_service(st, si)
            elif st.virtualization_type == 'vrouter-instance':
                self.vrouter_manager.create_service(st, si)
            elif st.virtualization_type == 'physical-device':
                self.ps_manager.create_service(st, si)
            else:
                self.logger.error("Unknown virt type: %s" %
                                      st.virtualization_type)
        except Exception:
            cgitb_error_log(self)
        si.launch_count += 1
        self.logger.info("SI %s creation success" % (':').join(si.fq_name))

    def delete_service_instance(self, vm):
        self.logger.info("Deleting VM %s %s for SI %s" %
            ((':').join(vm.fq_name), vm.uuid, vm.service_id))

        try:
            if vm.virtualization_type == svc_info.get_vm_instance_type():
                self.vm_manager.delete_service(vm)
            elif vm.virtualization_type == svc_info.get_netns_instance_type():
                self.netns_manager.delete_service(vm)
            elif vm.virtualization_type == 'vrouter-instance':
                self.vrouter_manager.delete_service(vm)
            elif vm.virtualization_type == 'physical-device':
                self.ps_manager.delete_service(vm)
            self.logger.info("Deleted VM %s %s for SI %s" %
                ((':').join(vm.fq_name), vm.uuid, vm.service_id))
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
        self.create_service_instance(si)

    def _check_service_running(self, si):
        st = ServiceTemplateSM.get(si.service_template)
        if st.params and st.params.get('version', 1) == 2:
            return
        if st.virtualization_type == 'virtual-machine':
            status = self.vm_manager.check_service(si)
        elif st.virtualization_type == 'network-namespace':
            status = self.netns_manager.check_service(si)
        elif st.virtualization_type == 'vrouter-instance':
            status = self.vrouter_manager.check_service(si)
        elif st.virtualization_type == 'physical-device':
            status = self.ps_manager.check_service(si)
        return status

    def delete_interface_route_table(self, irt_uuid):
        try:
            self._vnc_lib.interface_route_table_delete(id=irt_uuid)
            InterfaceRouteTableSM.delete(irt_uuid)
        except (NoIdError, RefsExistError):
            return

    def _delete_shared_vn(self, vn_uuid):
        try:
            self.logger.info("Deleting vn %s" % (vn_uuid))
            self._vnc_lib.virtual_network_delete(id=vn_uuid)
            VirtualNetworkSM.delete(vn_uuid)
        except (NoIdError, RefsExistError):
            pass

    @staticmethod
    def reset():
        for cls in DBBaseSM.get_obj_type_map().values():
            cls.reset()

    def sighup_handler(self):
        if self._conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(self._conf_file)
            if 'DEFAULTS' in config.sections():
                try:
                    collectors = config.get('DEFAULTS', 'collectors')
                    if type(collectors) is str:
                        collectors = collectors.split()
                        new_chksum = hashlib.md5("".join(collectors)).hexdigest()
                        if new_chksum != self._chksum:
                            self._chksum = new_chksum
                            config.random_collectors = random.sample(collectors, len(collectors))
                            self.logger.sandesh_reconfig_collectors(config)
                except ConfigParser.NoOptionError as e:
                     pass
    # end sighup_handler

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
    # delete orphan shared iips
    iip_delete_list = []
    for iip in InstanceIpSM.values():
        if not iip.instance_ip_secondary or not iip.service_instance_ip:
            continue
        if iip.service_instance:
            continue
        if len(iip.virtual_machine_interfaces):
            continue
        iip_delete_list.append(iip)
    for iip in iip_delete_list:
        monitor.port_tuple_agent.delete_shared_iip(iip)

    # delete vms without si
    vm_delete_list = []
    for vm in VirtualMachineSM.values():
        si = ServiceInstanceSM.get(vm.service_instance)
        if not si and vm.virtualization_type:
            vm_delete_list.append(vm)
    for vm in vm_delete_list:
        monitor.delete_service_instance(vm)

    # delete vmis with si but no vms
    vmi_delete_list = []
    for vmi in VirtualMachineInterfaceSM.values():
        si = ServiceInstanceSM.get(vmi.service_instance)
        if si and not vmi.virtual_machine:
            vmi_delete_list.append(vmi.uuid)
    if len(vmi_delete_list):
        monitor.vm_manager.cleanup_svc_vm_ports(vmi_delete_list)

    # check vrouter agent status
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


def launch_timer(monitor):
    if not monitor._args.check_service_interval.isdigit():
        monitor.logger.emergency("set seconds for check_service_interval "
                                     "in contrail-svc-monitor.conf. \
                                        example: check_service_interval=60")
        sys.exit()
    monitor.logger.notice("check_service_interval set to %s seconds" %
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
    monitor.logger.log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)


def parse_args(args_str):
    '''
    Eg. python svc_monitor.py --rabbit_server localhost
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
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'api_server_use_ssl': False,
        'zk_server_ip': '127.0.0.1',
        'zk_server_port': '2181',
        'collectors': None,
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
        'logging_conf': '',
        'logger_class': None,
        'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
        'check_service_interval': '60',
        'nova_endpoint_type': 'internalURL',
        'rabbit_use_ssl': False,
        'kombu_ssl_version': '',
        'kombu_ssl_keyfile': '',
        'kombu_ssl_certfile': '',
        'kombu_ssl_ca_certs': '',
    }
    secopts = {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
    }
    ksopts = {
        'auth_host': '127.0.0.1',
        'auth_protocol': 'http',
        'auth_port': '5000',
        'auth_version': 'v2.0',
        'auth_insecure': True,
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'admin'
    }
    schedops = {
        'si_netns_scheduler_driver':
        'svc_monitor.scheduler.vrouter_scheduler.RandomScheduler',
        'analytics_server_list': '127.0.0.1:8081',
        'availability_zone': None,
        'netns_availability_zone': None,
        'aaa_mode': cfgm_common.AAA_MODE_DEFAULT_VALUE,
    }
    cassandraopts = {
        'cassandra_user': None,
        'cassandra_password': None,
    }
    sandeshopts = {
        'sandesh_keyfile': '/etc/contrail/ssl/private/server-privkey.pem',
        'sandesh_certfile': '/etc/contrail/ssl/certs/server.pem',
        'sandesh_ca_cert': '/etc/contrail/ssl/certs/ca-cert.pem',
        'sandesh_ssl_enable': False,
        'introspect_ssl_enable': False
    }

    saved_conf_file = args.conf_file
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
        if 'CASSANDRA' in config.sections():
            cassandraopts.update(dict(config.items('CASSANDRA')))
        if 'SANDESH' in config.sections():
            sandeshopts.update(dict(config.items('SANDESH')))
            if 'sandesh_ssl_enable' in config.options('SANDESH'):
                sandeshopts['sandesh_ssl_enable'] = config.getboolean(
                    'SANDESH', 'sandesh_ssl_enable')
            if 'introspect_ssl_enable' in config.options('SANDESH'):
                sandeshopts['introspect_ssl_enable'] = config.getboolean(
                    'SANDESH', 'introspect_ssl_enable')

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
    defaults.update(cassandraopts)
    defaults.update(sandeshopts)
    parser.set_defaults(**defaults)

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
    parser.add_argument("--aaa_mode",
                        choices=cfgm_common.AAA_MODE_VALID_VALUES,
                        help="AAA mode")
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
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))
    parser.add_argument("--cassandra_user",
                        help="Cassandra user name")
    parser.add_argument("--cassandra_password",
                        help="Cassandra password")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
                        help="Sandesh send rate limit in messages/sec.")
    parser.add_argument("--check_service_interval",
                        help="Check service interval")

    args = parser.parse_args(remaining_argv)
    args._conf_file = saved_conf_file
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
    args.sandesh_config = SandeshConfig(args.sandesh_keyfile,
        args.sandesh_certfile, args.sandesh_ca_cert,
        args.sandesh_ssl_enable, args.introspect_ssl_enable)

    return args


def run_svc_monitor(sm_logger, args=None):
    sm_logger.notice("Elected master SVC Monitor node. Initializing... ")

    monitor = SvcMonitor(sm_logger, args)
    monitor._zookeeper_client = _zookeeper_client
    monitor._conf_file = args._conf_file
    monitor._chksum = ""
    if args.collectors:
        monitor._chksum = hashlib.md5("".join(args.collectors)).hexdigest()

    """ @sighup
    SIGHUP handler to indicate configuration changes
    """
    gevent.signal(signal.SIGHUP, monitor.sighup_handler)

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
            monitor.logger.api_conn_status_update(
                ConnectionStatus.DOWN, str(e))
            time.sleep(3)
        except (RuntimeError, ResourceExhaustionError):
            # auth failure or haproxy throws 503
            time.sleep(3)

    try:
        monitor.post_init(vnc_api, args)
        timer_task = gevent.spawn(launch_timer, monitor)
        gevent.joinall([timer_task])
    except KeyboardInterrupt:
        monitor.rabbit.close()
        raise


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

    # randomize collector list
    args.random_collectors = args.collectors
    if args.collectors:
        args.random_collectors = random.sample(args.collectors,
                                               len(args.collectors))

    # Initialize logger
    sm_logger = ServiceMonitorLogger(args)

    # Initialize AMQP handler then close it to be sure remain queue of a
    # precedent run is cleaned
    vnc_amqp = VncAmqpHandle(sm_logger, DBBaseSM, REACTION_MAP, 'svc_monitor',
                             args=args)
    vnc_amqp.establish()
    vnc_amqp.close()
    sm_logger.debug("Removed remained AMQP queue")

    # Waiting to be elected as master node
    _zookeeper_client = ZookeeperClient(
        client_pfx+"svc-monitor", args.zk_server_ip)
    sm_logger.notice("Waiting to be elected as master...")
    _zookeeper_client.master_election(zk_path_pfx+"/svc-monitor", os.getpid(),
                                      run_svc_monitor, sm_logger, args)
# end main


def server_main():
    vnc_cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
