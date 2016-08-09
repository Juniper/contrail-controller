#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of managing physical router configuration
"""

# Import kazoo.client before monkey patching
from cfgm_common.zkclient import ZookeeperClient
from gevent import monkey
monkey.patch_all()
from cfgm_common.vnc_kombu import VncKombuClient
import cgitb
import sys
import argparse
import requests
import ConfigParser
import socket
import time
from pprint import pformat

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.virtual_network.ttypes import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, \
    NodeTypeNames, INSTANCE_ID_DEFAULT
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
import discoveryclient.client as client
from cfgm_common.exceptions import ResourceExhaustionError
from vnc_api.vnc_api import VncApi
from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, \
    NodeStatus
from db import DBBaseDM, BgpRouterDM, PhysicalRouterDM, PhysicalInterfaceDM,\
    ServiceInstanceDM, LogicalInterfaceDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM, RoutingInstanceDM, GlobalSystemConfigDM, \
    GlobalVRouterConfigDM, FloatingIpDM, InstanceIpDM, DMCassandraDB, PortTupleDM
from physical_router_config import PushConfigState
from cfgm_common.dependency_tracker import DependencyTracker
from cfgm_common.utils import cgitb_hook


class DeviceManager(object):
    _REACTION_MAP = {
        'physical_router': {
            'self': ['bgp_router',
                     'physical_interface',
                     'logical_interface'],
            'bgp_router': [],
            'physical_interface': [],
            'logical_interface': [],
            'virtual_network': [],
            'global_system_config': [],
        },
        'global_system_config': {
            'self': ['physical_router'],
            'physical_router': [],
        },
        'bgp_router': {
            'self': ['bgp_router', 'physical_router'],
            'bgp_router': ['physical_router'],
            'physical_router': [],
        },
        'physical_interface': {
            'self': ['physical_router',
                     'physical_interface',
                     'logical_interface'],
            'physical_router': ['logical_interface'],
            'logical_interface': ['physical_router'],
            'physical_interface': ['physical_router'],
            'virtual_machine_interface': ['physical_interface'],
        },
        'logical_interface': {
            'self': ['physical_router',
                     'physical_interface',
                     'virtual_machine_interface'],
            'physical_interface': ['virtual_machine_interface'],
            'virtual_machine_interface': ['physical_router',
                                          'physical_interface'],
            'physical_router': ['virtual_machine_interface']
        },
        'virtual_machine_interface': {
            'self': ['logical_interface',
                     'physical_interface',
                     'virtual_network',
                     'floating_ip',
                     'instance_ip',
                     'port_tuple'],
            'logical_interface': ['virtual_network'],
            'virtual_network': ['logical_interface'],
            'floating_ip': ['virtual_network'],
            'instance_ip': ['virtual_network'],
            'routing_instance': ['port_tuple','physical_interface'],
            'port_tuple': ['physical_interface']
        },
        'service_instance': {
            'self': ['port_tuple'],
            'port_tuple':[],
        },
        'port_tuple':{
            'self':['virtual_machine_interface','service_instance'],
            'service_instance':['virtual_machine_interface'],
            'virtual_machine_interface':['service_instance']
        },
        'virtual_network': {
            'self': ['physical_router',
                     'virtual_machine_interface'],
            'routing_instance': ['physical_router',
                                 'virtual_machine_interface'],
            'physical_router': [],
            'virtual_machine_interface': ['physical_router'],
        },
        'routing_instance': {
            'self': ['routing_instance',
                     'virtual_network',
                     'virtual_machine_interface'],
            'routing_instance': ['virtual_network',
                                 'virtual_machine_interface'],
            'virtual_network': []
        },
        'floating_ip': {
            'self': ['virtual_machine_interface'],
            'virtual_machine_interface': [],
        },
        'instance_ip': {
            'self': ['virtual_machine_interface'],
            'virtual_machine_interface': [],
        },
    }

    def __init__(self, args=None):
        self._args = args

        # Initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(
                self._args.disc_server_ip,
                self._args.disc_server_port,
                ModuleNames[Module.DEVICE_MANAGER])
        
        PushConfigState.set_repush_interval(int(self._args.repush_interval))
        PushConfigState.set_repush_max_interval(int(self._args.repush_max_interval))
        PushConfigState.set_push_delay_per_kb(float(self._args.push_delay_per_kb))
        PushConfigState.set_push_delay_max(int(self._args.push_delay_max))
        PushConfigState.set_push_delay_enable(bool(self._args.push_delay_enable))

        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit value
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit(
                self._args.sandesh_send_rate_limit)
        module = Module.DEVICE_MANAGER
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        self.table = "ObjectConfigNode"
        instance_id = INSTANCE_ID_DEFAULT
        hostname = socket.gethostname()
        self._sandesh.init_generator(
            module_name, hostname, node_type_name, instance_id,
            self._args.collectors, 'to_bgp_context',
            int(args.http_server_port),
            ['cfgm_common', 'device_manager.sandesh'], self._disc)
        self._sandesh.set_logging_params(enable_local_log=args.log_local,
                                         category=args.log_category,
                                         level=args.log_level,
                                         file=args.log_file,
                                         enable_syslog=args.use_syslog,
                                         syslog_facility=args.syslog_facility)
        PhysicalRouterDM._sandesh = self._sandesh
        ConnectionState.init(
            self._sandesh, hostname, module_name, instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus, self.table)

        # Retry till API server is up
        connected = False
        self.connection_state_update(ConnectionStatus.INIT)
        while not connected:
            try:
                self._vnc_lib = VncApi(
                    args.admin_user, args.admin_password,
                    args.admin_tenant_name, args.api_server_ip,
                    args.api_server_port,
                    api_server_use_ssl=args.api_server_use_ssl)
                connected = True
                self.connection_state_update(ConnectionStatus.UP)
            except requests.exceptions.ConnectionError as e:
                # Update connection info
                self.connection_state_update(ConnectionStatus.DOWN, str(e))
                time.sleep(3)
            except ResourceExhaustionError:  # haproxy throws 503
                time.sleep(3)

        rabbit_servers = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost
        rabbit_ha_mode = self._args.rabbit_ha_mode

        self._db_resync_done = gevent.event.Event()

        q_name = 'device_manager.%s' % (socket.gethostname())
        self._vnc_kombu = VncKombuClient(rabbit_servers, rabbit_port,
                                         rabbit_user, rabbit_password,
                                         rabbit_vhost, rabbit_ha_mode,
                                         q_name, self._vnc_subscribe_callback,
                                         self.config_log, rabbit_use_ssl =
                                         self._args.rabbit_use_ssl,
                                         kombu_ssl_version =
                                         self._args.kombu_ssl_version,
                                         kombu_ssl_keyfile =
                                         self._args.kombu_ssl_keyfile,
                                         kombu_ssl_certfile =
                                         self._args.kombu_ssl_certfile,
                                         kombu_ssl_ca_certs =
                                         self._args.kombu_ssl_ca_certs)

        self._cassandra = DMCassandraDB.getInstance(self, _zookeeper_client)

        DBBaseDM.init(self, self._sandesh.logger(), self._cassandra)
        for obj in GlobalSystemConfigDM.list_obj():
            GlobalSystemConfigDM.locate(obj['uuid'], obj)

        for obj in GlobalVRouterConfigDM.list_obj():
            GlobalVRouterConfigDM.locate(obj['uuid'], obj)

        for obj in VirtualNetworkDM.list_obj():
            vn = VirtualNetworkDM.locate(obj['uuid'], obj)
            if vn is not None and vn.routing_instances is not None:
                for ri_id in vn.routing_instances:
                    ri_obj = RoutingInstanceDM.locate(ri_id)

        for obj in BgpRouterDM.list_obj():
            BgpRouterDM.locate(obj['uuid'], obj)

        pr_obj_list = PhysicalRouterDM.list_obj()
        pr_uuid_set = set([pr_obj['uuid'] for pr_obj in pr_obj_list])
        self._cassandra.handle_pr_deletes(pr_uuid_set)

        for obj in PortTupleDM.list_obj():
            PortTupleDM.locate(obj['uuid'],obj)

        for obj in pr_obj_list:
            pr = PhysicalRouterDM.locate(obj['uuid'], obj)
            li_set = pr.logical_interfaces
            vmi_set = set()
            for pi_id in pr.physical_interfaces:
                pi = PhysicalInterfaceDM.locate(pi_id)
                if pi:
                    li_set |= pi.logical_interfaces
                    vmi_set |= pi.virtual_machine_interfaces
            for li_id in li_set:
                li = LogicalInterfaceDM.locate(li_id)
                if li and li.virtual_machine_interface:
                    vmi_set |= set([li.virtual_machine_interface])
            for vmi_id in vmi_set:
                vmi = VirtualMachineInterfaceDM.locate(vmi_id)

        si_obj_list = ServiceInstanceDM.list_obj()
        si_uuid_set = set([si_obj['uuid'] for si_obj in si_obj_list])
        self._cassandra.handle_pnf_resource_deletes(si_uuid_set)

        for obj in si_obj_list:
            ServiceInstanceDM.locate(obj['uuid'], obj)

        for obj in InstanceIpDM.list_obj():
            InstanceIpDM.locate(obj['uuid'], obj)

        for obj in FloatingIpDM.list_obj():
            FloatingIpDM.locate(obj['uuid'], obj)

        for vn in VirtualNetworkDM.values():
            vn.update_instance_ip_map()

        for pr in PhysicalRouterDM.values():
            pr.set_config_state()

        self._db_resync_done.set()
        gevent.joinall(self._vnc_kombu.greenlets())
    # end __init__

    def connection_state_update(self, status, message=None):
        ConnectionState.update(
            conn_type=ConnType.APISERVER, name='ApiServer',
            status=status, message=message or '',
            server_addrs=['%s:%s' % (self._args.api_server_ip,
                                     self._args.api_server_port)])
    # end connection_state_update

    def config_log(self, msg, level):
        self._sandesh.logger().log(SandeshLogger.get_py_logger_level(level),
                                   msg)

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        dependency_tracker = None
        try:
            msg = "Notification Message: %s" % (pformat(oper_info))
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            obj_type = oper_info['type'].replace('-', '_')
            obj_class = DBBaseDM.get_obj_type_map().get(obj_type)
            if obj_class is None:
                return

            if oper_info['oper'] == 'CREATE':
                obj_dict = oper_info['obj_dict']
                obj_id = obj_dict['uuid']
                self._cassandra.cache_uuid_to_fq_name_add(
                    obj_id, obj_dict['fq_name'], obj_type)
                obj = obj_class.locate(obj_id, obj_dict)
                dependency_tracker = DependencyTracker(
                    DBBaseDM.get_obj_type_map(), self._REACTION_MAP)
                dependency_tracker.evaluate(obj_type, obj)
            elif oper_info['oper'] == 'UPDATE':
                obj_id = oper_info['uuid']
                obj = obj_class.get(obj_id)
                old_dt = None
                if obj is not None:
                    old_dt = DependencyTracker(
                        DBBaseDM.get_obj_type_map(), self._REACTION_MAP)
                    old_dt.evaluate(obj_type, obj)
                else:
                    obj = obj_class.locate(obj_id)
                obj.update()
                dependency_tracker = DependencyTracker(
                    DBBaseDM.get_obj_type_map(), self._REACTION_MAP)
                dependency_tracker.evaluate(obj_type, obj)
                if old_dt:
                    for resource, ids in old_dt.resources.items():
                        if resource not in dependency_tracker.resources:
                            dependency_tracker.resources[resource] = ids
                        else:
                            dependency_tracker.resources[resource] = list(
                                set(dependency_tracker.resources[resource]) |
                                set(ids))
            elif oper_info['oper'] == 'DELETE':
                obj_id = oper_info['uuid']
                self._cassandra.cache_uuid_to_fq_name_del(obj_id)
                obj = obj_class.get(obj_id)
                if obj is None:
                    return
                dependency_tracker = DependencyTracker(
                    DBBaseDM.get_obj_type_map(), self._REACTION_MAP)
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
            string_buf = cStringIO.StringIO()
            cgitb_hook(file=string_buf, format="text")
            self.config_log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)

        if not dependency_tracker:
            return

        for vn_id in dependency_tracker.resources.get('virtual_network', []):
            vn = VirtualNetworkDM.get(vn_id)
            if vn is not None:
                vn.update_instance_ip_map()

        for pr_id in dependency_tracker.resources.get('physical_router', []):
            pr = PhysicalRouterDM.get(pr_id)
            if pr is not None:
                pr.set_config_state()

    # end _vnc_subscribe_callback


def parse_args(args_str):
    '''
    Eg. python device_manager.py  --rabbit_server localhost
                         -- rabbit_port 5672
                         -- rabbit_user guest
                         -- rabbit_password guest
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
                         --use_syslog
                         --syslog_facility LOG_USER
                         --cluster_id <testbed-name>
                         --repush_interval 15
                         --repush_max_interval 300
                         --push_delay_per_kb 0.01
                         --push_delay_max 100
                         --push_delay_enable True
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we      all options in response to -h
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
        'disc_server_ip': None,
        'disc_server_port': None,
        'http_server_port': '8096',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
        'repush_interval': '15',
        'repush_max_interval': '600',
        'push_delay_per_kb': '0.01',
        'push_delay_max': '100',
        'push_delay_enable': 'True',
        'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
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
        'ifmap_certauth_port': "8444",
    }
    ksopts = {
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'default-domain',
    }
    cassandraopts = {
        'cassandra_user': None,
        'cassandra_password': None
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))
        if 'CASSANDRA' in config.sections():
            cassandraopts.update(dict(config.items('CASSANDRA')))

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(secopts)
    defaults.update(ksopts)
    defaults.update(cassandraopts)
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
    parser.add_argument("--zk_server_ip",
                        help="IP address:port of zookeeper server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
    parser.add_argument("--disc_server_ip",
                        help="IP address of the discovery server")
    parser.add_argument("--disc_server_port",
                        help="Port of the discovery server")
    parser.add_argument("--http_server_port",
                        help="Port of local HTTP server")
    parser.add_argument("--log_local", action="store_true",
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
    parser.add_argument("--cluster_id",
                        help="Used for database keyspace separation")
    parser.add_argument("--repush_interval",
                        help="time interval for config re push")
    parser.add_argument("--repush_max_interval",
                        help="max time interval for config re push")
    parser.add_argument("--push_delay_per_kb",
                        help="time delay between two successful commits per kb config size")
    parser.add_argument("--push_delay_max",
                        help="max time delay between two successful commits")
    parser.add_argument("--push_delay_enable",
                        help="enable delay between two successful commits")
    parser.add_argument("--cassandra_user",
                        help="Cassandra user name")
    parser.add_argument("--cassandra_password",
                        help="Cassandra password")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
                        help="Sandesh send rate limit in messages/sec")
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()

    return args
# end parse_args


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
    _zookeeper_client = ZookeeperClient(client_pfx+"device-manager",
                                        args.zk_server_ip)
    _zookeeper_client.master_election(zk_path_pfx+"/device-manager",
                                      os.getpid(), run_device_manager,
                                      args)
# end main


def run_device_manager(args):
    device_manager = DeviceManager(args)
# end run_device_manager


def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
