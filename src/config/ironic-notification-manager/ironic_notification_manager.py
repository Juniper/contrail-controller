#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of managing ironic notifications
"""

import gevent
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
import traceback
from pprint import *
import json
import pprint

from neutronclient.client import HTTPClient

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, \
    NodeTypeNames, INSTANCE_ID_DEFAULT, ServiceHttpPortMap
from sandesh_common.vns.constants import *
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from vnc_api.vnc_api import VncApi
from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from sandesh.ironic_notification_manager.ttypes import *

from ironic_kombu import IronicKombuClient


class IronicNotificationManager(object):

    _ironic_notification_manager = None
    _ironic_kombu_client = None

    def __init__(self, inm_logger=None, args=None):
        self._args = args

        IronicNotificationManager._ironic_notification_manager = self
    # end __init__

    def sync_with_ironic(self):

        auth_url = '%s://%s:%s/%s' % (self._args.auth_protocol,
                                      self._args.auth_server,
                                      self._args.auth_port,
                                      self._args.auth_version)
        print auth_url

        httpclient = HTTPClient(username=self._args.admin_user,
                                project_name=self._args.admin_tenant_name,
                                password=self._args.admin_password,
                                auth_url=auth_url)

        httpclient.authenticate()
        ironic_url = 'http://%s:%s/v1/nodes/detail' % (self._args.ironic_server_ip,
                                                       self._args.ironic_server_port)
        auth_token = httpclient.auth_token
        headers = {'X-Auth-Token': str(auth_token)}

        resp = requests.get(ironic_url, headers=headers)

        resp_dict = resp.json()
        node_dict_list = resp_dict["nodes"]
        self.process_ironic_node_info(node_dict_list)


    def sandesh_init(self):
        # Inventory node module initialization part
        try:
            __import__('sandesh.ironic_notification_manager')
            module = Module.IRONIC_NOTIF_MANAGER
        except ImportError as e:
            raise e

        try:
            module_name = ModuleNames[module]
            node_type = Module2NodeType[module]
            node_type_name = NodeTypeNames[node_type]
            instance_id = INSTANCE_ID_DEFAULT
            sandesh_package_list = ['sandesh.ironic_notification_manager']

            # In case of multiple collectors, use a randomly chosen one
            self.random_collectors = self._args.collectors
            if self._args.collectors:
                self._chksum = hashlib.md5("".join(self._args.collectors)).hexdigest()
                self.random_collectors = random.sample(self._args.collectors, \
                                                       len(self._args.collectors))
            sandesh_global.init_generator(
                module_name,
                socket.gethostname(),
                node_type_name,
                instance_id,
                self.random_collectors,
                module_name,
                self._args.introspect_port,
                sandesh_package_list)
            sandesh_global.set_logging_params(
                enable_local_log=self._args.log_local,
                category=self._args.log_category,
                level=self._args.log_level,
                file=self._args.log_file,
                enable_syslog=self._args.use_syslog,
                syslog_facility=self._args.syslog_facility)
            self._sandesh_logger = sandesh_global._logger
            self._sandesh_logger.info('Initialized Sandesh on Collector: %s' % \
                              self.random_collectors)
        except Exception as e:
            raise e

    def process_ironic_node_info(self, node_dict_list):
        ironic_node_list_sandesh = IronicNodeList()
        ironic_nodes = []

        for node_dict in node_dict_list:
            IronicNodeDict = dict()
            DriverInfoDict = dict()
            InstanceInfoDict = dict()
            NodePropertiesDict = dict()

            IronicNodeDictKeyMap = {
                'uuid': 'uuid',
                'provision_state': 'provision_state',
                'power_state': 'power_state',
                'driver': 'driver',
                'instance_uuid': 'instance_uuid',
                'name': 'name',
                'network_interface': 'network_interface',
                'event_type': 'event_type',
                'publisher_id': 'publisher_id',
                'maintenance': 'maintenance',
                'provision_updated_at': 'provision_update_timestamp',
                'updated_at': 'update_timestamp',
                'created_at': 'create_timestamp',
                'driver_info': 'driver_info',
                'instance_info': 'instance_info',
                'properties': 'properties'}
            sub_dict_list = ['driver_info', 'instance_info', 'properties']
            SubDictKeyMap = {
                'driver_info': ['ipmi_address', 'ipmi_password', 'ipmi_username',
                                'ipmi_terminal_port', 'deploy_kernel', 'deploy_ramdisk'],
                'instance_info': ['display_name', 'nova_host_id', 'configdrive',
                                  'root_gb', 'memory_mb', 'vcpus', 'local_gb',
                                  'image_checksum', 'image_source', 'image_type', 'image_url'],
                'properties': ['cpu_arch', 'cpus', 'local_gb', 'memory_mb', 'capabilities']
            }
            for key in IronicNodeDictKeyMap.keys():
                if key in node_dict and key not in sub_dict_list:
                    IronicNodeDict[IronicNodeDictKeyMap[key]] = node_dict[key]

            for sub_dict in sub_dict_list:
                IronicNodeDict[sub_dict] = {}
                if sub_dict in node_dict.keys():
                    for key in node_dict[sub_dict]:
                        if key in SubDictKeyMap[sub_dict]:
                            IronicNodeDict[sub_dict][key] = node_dict[sub_dict][key]

            DriverInfoDict = IronicNodeDict.pop("driver_info",{})
            InstanceInfoDict = IronicNodeDict.pop("instance_info",{})
            NodePropertiesDict = IronicNodeDict.pop("properties",{})

            driver_info = DriverInfo(**DriverInfoDict)
            instance_info = InstanceInfo(**InstanceInfoDict)
            node_properties = NodeProperties(**NodePropertiesDict)
            ironic_node_sandesh = IronicNode(**IronicNodeDict)
            ironic_node_sandesh.driver_info = driver_info
            ironic_node_sandesh.instance_info = instance_info
            ironic_node_sandesh.node_properties = node_properties
            ironic_nodes.append(ironic_node_sandesh)

            self._sandesh_logger.info('\nIronic Node Info: %s' %
                                     IronicNodeDict)
            self._sandesh_logger.info('\nIronic Driver Info: %s' %
                                     DriverInfoDict)
            self._sandesh_logger.info('\nIronic Instance Info: %s' %
                                     InstanceInfoDict)
            self._sandesh_logger.info('\nNode Properties: %s' %
                                     NodePropertiesDict)

        ironic_node_list_sandesh.ironic_nodes = ironic_nodes
        ironic_node_list_sandesh.name = node_dict["uuid"]
        ironic_sandesh_object = IronicNodeListInfo(data=ironic_node_list_sandesh)
        ironic_sandesh_object.send()

def parse_args(args_str):
    '''
    Eg. python ironic_notification_manager.py -c ironic-notification-manager.conf
    '''

    # Source any specified config/ini file
    # Turn off help, so we      all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'ironic_server_ip': '127.0.0.1',
        'ironic_server_port': 6385,
        'auth_server': '127.0.0.1',
        'auth_port': 5000,
        'auth_protocol': 'http',
        'auth_version': 'v2.0'
    }
    ksopts = {
      'admin_user': 'user1',
      'admin_password': 'password1',
      'admin_tenant_name': 'default-domain'
    }
    sandesh_opts = {
        'collectors'        : '127.0.0.1:8086',
        'introspect_port'   : int(ServiceHttpPortMap[ModuleNames[Module.IRONIC_NOTIF_MANAGER]]),
        'log_level'         : 'SYS_INFO',
        'log_local'         : False,
        'log_category'      : '',
        'log_file'          : Sandesh._DEFAULT_LOG_FILE,
        'use_syslog'        : False,
        'syslog_facility'   : Sandesh._DEFAULT_SYSLOG_FACILITY
    }
    saved_conf_file = args.conf_file
    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))
        if 'SANDESH' in config.sections():
            sandesh_opts.update(dict(config.items("SANDESH")))

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
    defaults.update(ksopts)
    defaults.update(sandesh_opts)
    parser.set_defaults(**defaults)

    args = parser.parse_args(remaining_argv)
    args.conf_file = saved_conf_file
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    if type(args.introspect_port) is str:
        args.introspect_port = int(args.introspect_port)
    return args
# end parse_args

def main(args_str=None):

    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    # Create Ironic Notification Daemon and sync with Ironic
    ironic_notification_manager = IronicNotificationManager(args=args)
    ironic_notification_manager.sandesh_init()
    ironic_notification_manager.sync_with_ironic()
    # Create RabbitMQ Message reader
    # TODO: Enhance for Rabbit HA
    ironic_notification_manager._ironic_kombu_client =  IronicKombuClient(
        args.rabbit_server, args.rabbit_port,
        args.rabbit_user, args.rabbit_password,
        args.notification_level, ironic_notification_manager)
    ironic_notification_manager._ironic_kombu_client._start()

# end main

def server_main():
    main()
# end server_main

if __name__ == '__main__':
    server_main()
