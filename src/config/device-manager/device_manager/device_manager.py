#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of managing physical router configuration
"""

import gevent
# Import kazoo.client before monkey patching
from cfgm_common.zkclient import ZookeeperClient
from gevent import monkey
monkey.patch_all()
import sys
import argparse
import requests
import ConfigParser
import socket
import time
import hashlib
import signal
import random
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
from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.exceptions import NoIdError
from cfgm_common.vnc_db import DBBase
from vnc_api.vnc_api import VncApi
from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, \
    NodeStatus
from db import DBBaseDM, BgpRouterDM, PhysicalRouterDM, PhysicalInterfaceDM,\
    ServiceInstanceDM, LogicalInterfaceDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM, RoutingInstanceDM, GlobalSystemConfigDM, \
    GlobalVRouterConfigDM, FloatingIpDM, InstanceIpDM, DMCassandraDB, PortTupleDM
from dm_amqp import DMAmqpHandle
from physical_router_config import PushConfigState
from cfgm_common.dependency_tracker import DependencyTracker
from cfgm_common import vnc_cgitb
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_logger import ConfigServiceLogger
from logger import DeviceManagerLogger


# zookeeper client connection
_zookeeper_client = None


class DeviceManager(object):
    REACTION_MAP = {
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

    _device_manager = None

    def __init__(self, dm_logger=None, args=None):
        self._args = args
        PushConfigState.set_repush_interval(int(self._args.repush_interval))
        PushConfigState.set_repush_max_interval(int(self._args.repush_max_interval))
        PushConfigState.set_push_delay_per_kb(float(self._args.push_delay_per_kb))
        PushConfigState.set_push_delay_max(int(self._args.push_delay_max))
        PushConfigState.set_push_delay_enable(bool(self._args.push_delay_enable))

        self._chksum = "";
        if self._args.collectors:
            self._chksum = hashlib.md5(''.join(self._args.collectors)).hexdigest()

        # Initialize logger
        self.logger = dm_logger or DeviceManagerLogger(args)

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

        """ @sighup
        Handle of SIGHUP for collector list config change
        """
        gevent.signal(signal.SIGHUP, self.sighup_handler)

        # Initialize amqp
        self._vnc_amqp = DMAmqpHandle(self.logger, self.REACTION_MAP,
                                      self._args)
        self._vnc_amqp.establish()

        # Initialize cassandra
        self._object_db = DMCassandraDB.get_instance(self, _zookeeper_client)
        DBBaseDM.init(self, self.logger, self._object_db)
        DBBaseDM._sandesh = self.logger._sandesh

        for obj in GlobalSystemConfigDM.list_obj():
            GlobalSystemConfigDM.locate(obj['uuid'], obj)

        for obj in GlobalVRouterConfigDM.list_obj():
            GlobalVRouterConfigDM.locate(obj['uuid'], obj)

        for obj in VirtualNetworkDM.list_obj():
            VirtualNetworkDM.locate(obj['uuid'], obj)

        for obj in RoutingInstanceDM.list_obj():
            RoutingInstanceDM.locate(obj['uuid'], obj)

        for obj in BgpRouterDM.list_obj():
            BgpRouterDM.locate(obj['uuid'], obj)

        pr_obj_list = PhysicalRouterDM.list_obj()
        for obj in pr_obj_list:
            PhysicalRouterDM.locate(obj['uuid'],obj)

        pr_uuid_set = set([pr_obj['uuid'] for pr_obj in pr_obj_list])
        self._object_db.handle_pr_deletes(pr_uuid_set)

        for obj in PortTupleDM.list_obj():
            PortTupleDM.locate(obj['uuid'],obj)

        for obj in PhysicalInterfaceDM.list_obj():
            PhysicalInterfaceDM.locate(obj['uuid'],obj)

        for obj in LogicalInterfaceDM.list_obj():
            LogicalInterfaceDM.locate(obj['uuid'],obj)

        for obj in VirtualMachineInterfaceDM.list_obj():
            VirtualMachineInterfaceDM.locate(obj['uuid'],obj)

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
        self._object_db.handle_pnf_resource_deletes(si_uuid_set)

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

        DeviceManager._device_manager = self
        self._vnc_amqp._db_resync_done.set()
        try:
            gevent.joinall(self._vnc_amqp._vnc_kombu.greenlets())
        except KeyboardInterrupt:
            DeviceManager.destroy_instance()
            raise
    # end __init__

    @classmethod
    def get_instance(cls):
        return cls._device_manager

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if not inst:
            return
        inst._vnc_amqp.close()
        for obj_cls in DBBaseDM.get_obj_type_map().values():
            obj_cls.reset()
        DBBase.clear()
        DMCassandraDB.clear_instance()
        inst._object_db = None
        cls._device_manager = None

    def connection_state_update(self, status, message=None):
        ConnectionState.update(
            conn_type=ConnType.APISERVER, name='ApiServer',
            status=status, message=message or '',
            server_addrs=['%s:%s' % (self._args.api_server_ip,
                                     self._args.api_server_port)])
    # end connection_state_update

    # sighup handler for applying new configs
    def sighup_handler(self):
        if self._args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(self._args.conf_file)
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
        'http_server_port': '8096',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
        'logging_conf': '',
        'logger_class': None,
        'repush_interval': '15',
        'repush_max_interval': '600',
        'push_delay_per_kb': '0.01',
        'push_delay_max': '100',
        'push_delay_enable': True,
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
    sandeshopts = {
        'sandesh_keyfile': '/etc/contrail/ssl/private/server-privkey.pem',
        'sandesh_certfile': '/etc/contrail/ssl/certs/server.pem',
        'sandesh_ca_cert': '/etc/contrail/ssl/certs/ca-cert.pem',
        'sandesh_ssl_enable': False,
        'introspect_ssl_enable': False
    }

    saved_conf_file = args.conf_file
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
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(secopts)
    defaults.update(ksopts)
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
    parser.add_argument("--zk_server_ip",
                        help="IP address:port of zookeeper server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
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
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))
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
    args.sandesh_config = SandeshConfig(args.sandesh_keyfile,
        args.sandesh_certfile, args.sandesh_ca_cert,
        args.sandesh_ssl_enable, args.introspect_ssl_enable)

    args.conf_file = saved_conf_file
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

    # randomize collector list
    args.random_collectors = args.collectors
    if args.collectors:
        args.random_collectors = random.sample(args.collectors,
                                               len(args.collectors))

    # Initialize logger
    dm_logger = DeviceManagerLogger(args)

    # Initialize AMQP handler then close it to be sure remain queue of a
    # precedent run is cleaned
    vnc_amqp = DMAmqpHandle(dm_logger, DeviceManager.REACTION_MAP, args)
    vnc_amqp.establish()
    vnc_amqp.close()
    dm_logger.debug("Removed remained AMQP queue")

    _zookeeper_client = ZookeeperClient(client_pfx+"device-manager",
                                        args.zk_server_ip)
    dm_logger.notice("Waiting to be elected as master...")
    _zookeeper_client.master_election(zk_path_pfx+"/device-manager",
                                      os.getpid(), run_device_manager,
                                      dm_logger, args)
# end main


def run_device_manager(dm_logger, args):
    dm_logger.notice("Elected master Device Manager node. Initializing... ")
    DeviceManager(dm_logger, args)
# end run_device_manager


def server_main():
    vnc_cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
