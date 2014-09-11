#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor to instantiate/scale/monitor services like firewall, LB, ...
"""

import sys
import gevent
from gevent import monkey
monkey.patch_all(thread=not 'unittest' in sys.modules)

from cfgm_common.zkclient import ZookeeperClient
import requests
import ConfigParser
import cgitb
import argparse
import socket

import re
import os

import logging
import logging.handlers

from cfgm_common.imid import *
from cfgm_common import importutils
from cfgm_common import svc_info

from pysandesh.sandesh_base import Sandesh
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.gen_py.process_info.ttypes import ConnectionType, \
    ConnectionStatus

from vnc_api.vnc_api import *

import discoveryclient.client as client

from db import ServiceMonitorDB
from logger import ServiceMonitorLogger
from instance_manager import InstanceManager

# zookeeper client connection
_zookeeper_client = None

class SvcMonitor(object):

    """
    data + methods used/referred to by ssrc and arc greenlets
    """

    def __init__(self, args=None):
        self._args = args

        # create database and logger
        self.db = ServiceMonitorDB(args)

        # initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(self._args.disc_server_ip,
                                                self._args.disc_server_port,
                                                client_type='Service Monitor')

        # initialize logger
        self.logger = ServiceMonitorLogger(self.db, self._disc, args)
        self.db.add_logger(self.logger)

        # rotating log file for catchall errors
        self._err_file = '/var/log/contrail/svc-monitor.err'
        self._tmp_file = '/var/log/contrail/svc-monitor.tmp'
        try:
            with open(self._err_file, 'a'):
                pass
            with open(self._tmp_file, 'a'):
                pass
        except IOError:
            self._err_file = './svc-monitor.err'
            self._tmp_file = './svc-monitor.tmp'
        self._svc_err_logger = logging.getLogger('SvcErrLogger')
        self._svc_err_logger.setLevel(logging.ERROR)
        handler = logging.handlers.RotatingFileHandler(
            self._err_file, maxBytes=64*1024, backupCount=2)
        self._svc_err_logger.addHandler(handler)


    def post_init(self, vnc_lib, args=None):
        # api server
        self._vnc_lib = vnc_lib

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
                                      image_name='in-network',
                                      hypervisor_type='network-namespace',
                                      scaling=True)

        # load vrouter scheduler
        self.vrouter_scheduler = importutils.import_object(
            self._args.si_netns_scheduler_driver,
            self._vnc_lib,
            self._args)

        # load virtual machine instance manager
        self.vm_manager = importutils.import_object(
            'svc_monitor.instance_manager.VirtualMachineManager',
            self._vnc_lib, self.db, self.logger,
            self.vrouter_scheduler, self._args)

        # load network namespace instance manager
        self.netns_manager = importutils.import_object(
            'svc_monitor.instance_manager.NetworkNamespaceManager',
            self._vnc_lib, self.db, self.logger,
            self.vrouter_scheduler, self._args)


    # create service template
    def _create_default_template(self, st_name, svc_type, svc_mode=None,
                                 hypervisor_type='virtual-machine',
                                 image_name=None, flavor=None, scaling=False):
        domain_name = 'default-domain'
        domain_fq_name = [domain_name]
        st_fq_name = [domain_name, st_name]
        self.logger.log("Creating %s %s hypervisor %s" %
                         (domain_name, st_name, hypervisor_type))

        try:
            st_obj = self._vnc_lib.service_template_read(fq_name=st_fq_name)
            st_uuid = st_obj.uuid
            self.logger.log("%s exists uuid %s" % (st_name, str(st_uuid)))
            return
        except NoIdError:
            domain = self._vnc_lib.domain_read(fq_name=domain_fq_name)
            st_obj = ServiceTemplate(name=st_name, domain_obj=domain)
            st_uuid = self._vnc_lib.service_template_create(st_obj)

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
            if_list = [['left', False], ['right', False]]
        else:
            if_list = [
                ['management', False], ['left', False], ['right', False]]

        for itf in if_list:
            if_type = ServiceTemplateInterfaceType(shared_ip=itf[1])
            if_type.set_service_interface_type(itf[0])
            svc_properties.add_interface_type(if_type)

        try:
            st_obj.set_service_template_properties(svc_properties)
            self._vnc_lib.service_template_update(st_obj)
        except Exception as e:
            print e

        self.logger.log("%s created with uuid %s" % (st_name, str(st_uuid)))
    #_create_default_analyzer_template

    def cleanup(self):
        # TODO cleanup sandesh context
        pass
    # end cleanup

    def _get_proj_name_from_si_fq_str(self, si_fq_str):
        return si_fq_str.split(':')[1]
    # enf _get_si_fq_str_to_proj_name

    def _get_virtualization_type(self, st_props):
        return st_props.get_service_virtualization_type() or 'virtual-machine'
    # end _get_virtualization_type

    def _check_svc_vm_exists(self, si_obj):
        vm_back_refs = si_obj.get_virtual_machine_back_refs()
        if not vm_back_refs:
            return False
        si_props = si_obj.get_service_instance_properties()
        if si_props.get_scale_out():
            max_instances = si_props.get_scale_out().get_max_instances()
        else:
            max_instances = 1
        if max_instances == len(vm_back_refs):
            return True
        return False
    #end _check_svc_vm_exists

    def _check_store_si_info(self, st_obj, si_obj):
        config_complete = True
        st_props = st_obj.get_service_template_properties()
        st_if_list = st_props.get_interface_type()
        si_props = si_obj.get_service_instance_properties()
        si_if_list = si_props.get_interface_list()
        if si_if_list and (len(si_if_list) != len(st_if_list)):
            self.logger.log("Error: IF mismatch template %s instance %s" %
                             (len(st_if_list), len(si_if_list)))
            return

        # read existing si_entry
        si_entry = self.db.service_instance_get(si_obj.get_fq_name_str())
        if not si_entry:
            si_entry = {}
        si_entry['instance_type'] = self._get_virtualization_type(st_props)

        # walk the interface list
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

            if not si_vn_str:
                continue

            si_entry[itf_type + '-vn'] = si_vn_str
            try:
                vn_obj = self._vnc_lib.virtual_network_read(
                    fq_name_str=si_vn_str)
                if vn_obj.uuid != si_entry.get(si_vn_str, None):
                    si_entry[si_vn_str] = vn_obj.uuid
            except NoIdError:
                self.logger.log("Warn: VN %s add is pending" % si_vn_str)
                si_entry[si_vn_str] = 'pending'
                config_complete = False

        if config_complete:
            self.logger.log("SI %s info is complete" %
                             si_obj.get_fq_name_str())
        else:
            self.logger.log("Warn: SI %s info is not complete" %
                             si_obj.get_fq_name_str())

        #insert entry
        self.db.service_instance_insert(si_obj.get_fq_name_str(), si_entry)
        return config_complete
    #end _check_store_si_info

    def _restart_svc(self, vm_uuid, si_fq_str):
        proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
        self._delete_svc_instance(vm_uuid, proj_name, si_fq_str=si_fq_str)

        # Wait the clean phase was completely done before recreate the SI
        gevent.sleep(_CHECK_CLEANUP_INTERVAL + 1)

        si_obj = self._vnc_lib.service_instance_read(fq_name_str=si_fq_str)
        st_list = si_obj.get_service_template_refs()
        if st_list is not None:
            fq_name = st_list[0]['to']
            st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)
            self._create_svc_instance(st_obj, si_obj)
    # end _restart_svc

    def _delete_shared_vn(self, vn_uuid, proj_name):
        try:
            self.logger.log("Deleting VN %s %s" % (proj_name, vn_uuid))
            self._vnc_lib.virtual_network_delete(id=vn_uuid)
        except RefsExistError:
            self._svc_err_logger.error("Delete failed refs exist VN %s %s" %
                                       (proj_name, vn_uuid))
        except NoIdError:
            # remove from cleanup list
            self.db.cleanup_table_remove(vn_uuid)
    # end _delete_shared_vn

    def _create_svc_instance(self, st_obj, si_obj):
        #check if all config received before launch
        if not self._check_store_si_info(st_obj, si_obj):
            return

        #check if VMs already exist
        if self._check_svc_vm_exists(si_obj):
            return

        st_props = st_obj.get_service_template_properties()
        if st_props is None:
            self.logger.log("Cannot find service template associated to "
                             "service instance %s" % si_obj.get_fq_name_str())
        virt_type = self._get_virtualization_type(st_props)
        if virt_type == 'virtual-machine':
            self.vm_manager.create_service(st_obj, si_obj)
        elif virt_type == 'network-namespace':
            self.netns_manager.create_service(st_obj, si_obj)

    def _delete_svc_instance(self, vm_uuid, proj_name, si_fq_str=None):
        vm_entry = self.db.virtual_machine_get(vm_uuid)
        if not vm_entry: 
            self.db.cleanup_table_remove(vm_uuid)
            return

        self.logger.log("Deleting VM %s %s" % (proj_name, vm_uuid))
        virt_type = vm_entry['instance_type']
        if virt_type == 'virtual-machine':
            self.vm_manager.delete_service(vm_uuid, proj_name)
        elif virt_type == 'network-namespace':
            self.netns_manager.delete_service(vm_uuid)

        # remove from launch table and queue into cleanup list
        self.db.virtual_machine_remove(vm_uuid)
        self.db.cleanup_table_insert(
            vm_uuid, {'proj_name': proj_name, 'type': 'vm'})
        self.logger.uve_svc_instance(si_fq_str, status='DELETE', vm_uuid=vm_uuid)

    def _delmsg_project_service_instance(self, idents):
        proj_fq_str = idents['project']
        si_fq_str = idents['service-instance']

        si_info = self.db.service_instance_get(si_fq_str)
        if not si_info:
            return

        virt_type = si_info['instance_type']
        if virt_type == 'virtual-machine':
            self.vm_manager.clean_resources(proj_fq_str, si_fq_str)
        elif virt_type == 'network-namespace':
            self.netns_manager.clean_resources(proj_fq_str, si_fq_str)

        #delete si info
        self.db.service_instance_remove(si_fq_str)
    # end _delmsg_project_service_instance

    def _delmsg_service_instance_service_template(self, idents):
        si_fq_str = idents['service-instance']
        si_info = self.db.service_instance_get(si_fq_str)
        if not si_info:
            return

        vm_list = self.db.virtual_machine_list()
        for vm_uuid, si in vm_list:
            if si_fq_str != si['si_fq_str']:
                continue

            proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
            self._delete_svc_instance(vm_uuid, proj_name, si_fq_str=si_fq_str)

            #insert shared instance IP uuids into cleanup list if present
            for itf_str in svc_info.get_if_str_list():
                iip_uuid_str = itf_str + '-iip-uuid'
                if not iip_uuid_str in si_info:
                    continue
                self.db.cleanup_table_insert(
                    si_info[iip_uuid_str], {'proj_name': proj_name,
                                            'type': 'iip'})
    #end _delmsg_service_instance_service_template

    def _delmsg_virtual_machine_service_instance(self, idents):
        vm_uuid = idents['virtual-machine']
        si_fq_str = idents['service-instance']
        proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
        vm_list = self.db.virtual_machine_list()
        for vm_uuid, si in vm_list:
            if si_fq_str != si['si_fq_str']:
                continue

            self._delete_svc_instance(vm_uuid, proj_name, si_fq_str=si_fq_str)
    # end _delmsg_service_instance_virtual_machine

    def _delmsg_virtual_machine_interface_route_table(self, idents):
        rt_fq_str = idents['interface-route-table']

        rt_obj = self._vnc_lib.interface_route_table_read(
            fq_name_str=rt_fq_str)
        vmi_list = rt_obj.get_virtual_machine_interface_back_refs()
        if vmi_list is None:
            self._vnc_lib.interface_route_table_delete(id=rt_obj.uuid)
    # end _delmsg_virtual_machine_interface_route_table

    def _addmsg_virtual_machine_interface_virtual_machine(self, idents):
        vm_fq_str = idents['virtual-machine']

        try:
            vm_obj = self._vnc_lib.virtual_machine_read(
                fq_name_str=vm_fq_str)
        except NoIdError:
            return

        # check if this is a service vm
        si_list = vm_obj.get_service_instance_refs()
        if not si_list:
            return

        vm_info = self.db.virtual_machine_get(vm_obj.uuid)
        if not vm_info:
            return
        si_fq_str = vm_info['si_fq_str']

        try:
            si_obj = self._vnc_lib.service_instance_read(
                fq_name_str=si_fq_str)
        except NoIdError:
            proj_name = self._get_proj_name_from_si_fq_str(si_fq_str)
            self._delete_svc_instance(vm_obj.uuid, proj_name,
                                      si_fq_str=si_fq_str)
            return

        # create service instance to service vm link
        vm_obj.add_service_instance(si_obj)
        self._vnc_lib.virtual_machine_update(vm_obj)
        self.logger.log("Info: VM %s updated SI %s" %
                         (vm_obj.get_fq_name_str(), si_fq_str))
    # end _addmsg_virtual_machine_interface_virtual_machine

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
        self._create_svc_instance(st_obj, si_obj)
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

        si_list = self.db.service_instance_list()
        if not si_list:
            return

        for si_fq_str, si_info in si_list:
            if vn_fq_str not in si_info.keys():
                continue

            try:
                si_obj = self._vnc_lib.service_instance_read(
                    fq_name_str=si_fq_str)
                if si_obj.get_virtual_machine_back_refs():
                    continue

                st_refs = si_obj.get_service_template_refs()
                fq_name = st_refs[0]['to']
                st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)

                #launch VMs
                self._create_svc_instance(st_obj, si_obj)
            except Exception:
                continue
    #end _addmsg_project_virtual_network

    def process_poll_result(self, poll_result_str):
        result_list = parse_poll_result(poll_result_str)

        # process ifmap message
        for (result_type, idents, metas) in result_list:
            if 'ERROR' in idents.values():
                continue
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
                    self.logger.log("%s with %s/%s"
                                     % (funcname, meta_name, idents))
                    func(idents)
            # end for meta
        # end for result_type
    # end process_poll_result

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
    vm_list = monitor.db.virtual_machine_list()
    if not vm_list:
        return

    for vm_uuid, si in vm_list:
        try:
            status = None
            if si['instance_type'] == 'virtual-machine':
                proj_name = monitor._get_proj_name_from_si_fq_str(si['si_fq_str'])
                status = monitor.vm_manager.novaclient_oper('servers', 'find',
                    proj_name, id=vm_uuid).status
            elif si['instance_type'] == 'network-namespace':
                try:
                    if not monitor.vrouter_scheduler.vrouter_running(si['vrouter_name']):
                        # The scheduled vrouter is down, re-create it
                        status = 'ERROR'
                except KeyError:
                    # Cannot found the scheduled vrouter, re-create it
                    status = 'ERROR'
            if status and status == 'ERROR':
                monitor.logger.log("The VM %s of SI %s is not running. "
                                    "Re-create it." %
                                    (vm_uuid, si['si_fq_str']))
                monitor._restart_svc(vm_uuid, si['si_fq_str'])
        except Exception:
            continue
# end timer_callback


def launch_timer(monitor):
    while True:
        gevent.sleep(svc_info.get_vm_health_interval())
        try:
            timer_callback(monitor)
        except Exception:
            cgitb_error_log(monitor)
#end launch_timer


def cleanup_callback(monitor):
    delete_list = monitor.db.cleanup_table_list()
    if not delete_list:
        return

    for uuid, info in delete_list or []:
        if info['type'] == 'vm':
            monitor._delete_svc_instance(uuid, info['proj_name'])
        elif info['type'] == 'vn':
            monitor._delete_shared_vn(uuid, info['proj_name'])
# end cleanup_callback


def launch_cleanup(monitor):
    while True:
        gevent.sleep(svc_info.get_cleanup_interval())
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
                         --use_syslog
                         --syslog_facility LOG_USER
                         --cluster_id <testbed-name>
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
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'region_name': None,
        'cluster_id': '',
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
    schedops = {
        'si_netns_scheduler_driver': \
            'svc_monitor.scheduler.vrouter_scheduler.RandomScheduler',
        'analytics_server_ip': '127.0.0.1',
        'analytics_server_port': '8081',
        'availability_zone': None,
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
        if 'SCHEDULER' in config.sections():
            schedops.update(dict(config.items("SCHEDULER")))

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
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    if args.region_name and args.region_name.lower() == 'none':
        args.region_name = None
    if args.availability_zone and args.availability_zone.lower() == 'none':
        args.availability_zone = None
    return args
# end parse_args


def run_svc_monitor(args=None):
    monitor = SvcMonitor(args)

    # Retry till API server is up
    connected = False
    monitor.logger.api_conn_status_update(ConnectionStatus.INIT)

    while not connected:
        try:
            vnc_api = VncApi(
                args.admin_user, args.admin_password, args.admin_tenant_name,
                args.api_server_ip, args.api_server_port)
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
