#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of managing ironic notifications
"""

from __future__ import absolute_import

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
import cgitb

from ironicclient import client as ironicclient

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

from .sandesh.ironic_notification_manager.ttypes import *
from .ironic_kombu import IronicKombuClient


class IronicNotificationManager(object):

    _ironic_notification_manager = None
    _ironic_kombu_client = None

    IronicNodeDictKeyMap = {
        'uuid': 'name',
        'provision_state': 'provision_state',
        'power_state': 'power_state',
        'driver': 'driver',
        'instance_uuid': 'instance_uuid',
        'name': 'host_name',
        'network_interface': 'network_interface',
        'event_type': 'event_type',
        'publisher_id': 'publisher_id',
        'maintenance': 'maintenance',
        'provision_updated_at': 'provision_update_timestamp',
        'updated_at': 'update_timestamp',
        'created_at': 'create_timestamp',
        'driver_info': 'driver_info',
        'port_info': 'port_info',
        'instance_info': 'instance_info',
        'properties': 'properties'}
    PortInfoDictKeyMap = {
        'uuid': 'port_uuid',
        'pxe_enabled': 'pxe_enabled',
        'address': 'mac_address',
        'created_at': 'create_timestamp'
    }
    sub_dict_list = ['driver_info', 'instance_info', 'properties',
                     'port_info']
    SubDictKeyMap = {
        'driver_info': ['ipmi_address', 'ipmi_password', 'ipmi_username',
                        'ipmi_terminal_port', 'deploy_kernel',
                        'deploy_ramdisk'],
        'instance_info': ['display_name', 'nova_host_id', 'configdrive',
                          'root_gb', 'memory_mb', 'vcpus', 'local_gb',
                          'image_checksum', 'image_source', 'image_type',
                          'image_url'],
        'properties': ['cpu_arch', 'cpus', 'local_gb', 'memory_mb',
                       'capabilities'],
        'port_info': ['port_uuid', 'pxe_enabled', 'mac_address',
                      'local_link_connection', 'internal_info']
    }

    def __init__(self, inm_logger=None, args=None):
        self._args = args

        IronicNotificationManager._ironic_notification_manager = self
    # end __init__

    def authenticate_with_ironicclient(self):
        if self._args.auth_url:
            auth_url = self._args.auth_url
        else:
            auth_url = '%s://%s:%s/%s' % (self._args.auth_protocol,
                                          self._args.auth_host,
                                          self._args.auth_port,
                                          self._args.auth_version)
        kwargs = {
            'os_auth_url': auth_url,
            'os_username': self._args.admin_user,
            'os_password': self._args.admin_password,
            'os_project_name': self._args.admin_tenant_name,
            'os_endpoint_type': self._args.endpoint_type,
            'os_ironic_api_version': "1.19"
        }
        if 'v2.0' not in auth_url.split('/'):
            kwargs['os_user_domain_name'] = self._args.user_domain_name
            kwargs['os_project_domain_name'] = self._args.project_domain_name

        # TODO: Implement Keystone SSL support

        ironic_client_object = ironicclient.get_client(1, **kwargs)
        return ironic_client_object

    def sync_with_ironic(self):
        try:
            auth_ironic_client = self.authenticate_with_ironicclient()
        except Exception as e:
            raise e

        # Get and process Ironic Nodes
        node_dict_list = auth_ironic_client.node.list(detail=True)
        new_node_dict_list = []
        node_port_map = {}
        for node_dict in node_dict_list:
            new_node_dict_list.append(node_dict.to_dict())
            node_port_map[node_dict.to_dict()["uuid"]] = []
        self.process_ironic_node_info(new_node_dict_list)

        # Get and process Ports for all Ironic Nodes
        port_dict_list = auth_ironic_client.port.list(detail=True)
        new_port_dict_list = []
        for port_dict in port_dict_list:
            ironic_node_with_port_info = \
              self.process_ironic_port_info(port_dict.to_dict())
            node_port_map[ironic_node_with_port_info["name"]] +=\
              ironic_node_with_port_info["port_info"]
        for node_uuid in node_port_map.keys():
            IronicNodeDict = {"name": str(node_uuid),
                              "port_info": node_port_map[node_uuid]}
            ironic_node_sandesh = IronicNode(**IronicNodeDict)
            ironic_node_sandesh.name = IronicNodeDict["name"]
            ironic_sandesh_object = IronicNodeInfo(data=ironic_node_sandesh)
            ironic_sandesh_object.send()

    def sandesh_init(self):
        # Inventory node module initialization part
        try:
            __import__(
              'ironic_notification_manager.sandesh.ironic_notification_manager'
            )
            module = Module.IRONIC_NOTIF_MANAGER
        except ImportError as e:
            raise e

        try:
            module_name = ModuleNames[module]
            node_type = Module2NodeType[module]
            node_type_name = NodeTypeNames[node_type]
            instance_id = INSTANCE_ID_DEFAULT
            sandesh_package_list = [
              'ironic_notification_manager.sandesh.ironic_notification_manager'
            ]

            # In case of multiple collectors, use a randomly chosen one
            self.random_collectors = self._args.collectors
            if self._args.collectors:
                self._chksum = \
                    hashlib.md5("".join(self._args.collectors)).hexdigest()
                self.random_collectors = \
                    random.sample(self._args.collectors,
                                  len(self._args.collectors))
            if 'host_ip' in self._args:
                host_ip = self._args.host_ip
            else:
                host_ip = socket.gethostbyname(socket.getfqdn())
            sandesh_global.init_generator(
                module_name,
                socket.getfqdn(host_ip),
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
        except Exception as e:
            raise e

    def process_ironic_port_info(self, data_dict, IronicNodeDict=None):
        PortInfoDict = dict()
        PortList = []
        if not IronicNodeDict:
            IronicNodeDict = dict()

        for key in self.PortInfoDictKeyMap.keys():
            if key in data_dict:
                PortInfoDict[self.PortInfoDictKeyMap[key]] = \
                    data_dict[key]

        if "event_type" in data_dict and \
          str(data_dict["event_type"]) == "baremetal.port.delete.end":
            if "node_uuid" in data_dict:
              IronicNodeDict["name"] = data_dict["node_uuid"]
            IronicNodeDict["port_info"] = []
            return IronicNodeDict

        if "local_link_connection" in data_dict:
            local_link_connection = \
              LocalLinkConnection(**data_dict["local_link_connection"])
            data_dict.pop("local_link_connection")
            PortInfoDict["local_link_connection"] = local_link_connection

        if "internal_info" in data_dict:
            internal_info = \
              InternalPortInfo(**data_dict["internal_info"])
            data_dict.pop("internal_info")
            PortInfoDict["internal_info"] = internal_info

        if "node_uuid" in data_dict:
            IronicNodeDict["name"] = data_dict["node_uuid"]
            PortInfoDict["name"] = PortInfoDict["port_uuid"]
            PortList.append(PortInfo(**PortInfoDict))
            IronicNodeDict["port_info"] = PortList

        return IronicNodeDict

    def process_ironic_node_info(self, node_dict_list):

        for node_dict in node_dict_list:

            if not isinstance(node_dict, dict):
                node_dict = node_dict.to_dict()

            IronicNodeDict = dict()
            DriverInfoDict = dict()
            InstanceInfoDict = dict()
            NodePropertiesDict = dict()

            for key in self.IronicNodeDictKeyMap.keys():
                if key in node_dict and key not in self.sub_dict_list:
                    IronicNodeDict[self.IronicNodeDictKeyMap[key]] = \
                        node_dict[key]

            for sub_dict in self.sub_dict_list:
                IronicNodeDict[sub_dict] = {}
                if sub_dict in node_dict.keys():
                    for key in node_dict[sub_dict]:
                        if key in self.SubDictKeyMap[sub_dict]:
                            IronicNodeDict[sub_dict][key] = \
                                node_dict[sub_dict][key]

            if "event_type" in node_dict:
               if str(node_dict["event_type"]) == "baremetal.node.delete.end":
                   ironic_node_sandesh = IronicNode(**IronicNodeDict)
                   ironic_node_sandesh.deleted = True
                   ironic_node_sandesh.name = IronicNodeDict["name"]
                   ironic_sandesh_object = \
                     IronicNodeInfo(data=ironic_node_sandesh)
                   ironic_sandesh_object.send()
                   continue
               if "port" in str(node_dict["event_type"]):
                   IronicNodeDict = \
                       self.process_ironic_port_info(node_dict,
                                                     IronicNodeDict)

            DriverInfoDict = IronicNodeDict.pop("driver_info", None)
            InstanceInfoDict = IronicNodeDict.pop("instance_info", None)
            NodePropertiesDict = IronicNodeDict.pop("properties", None)
            PortInfoDictList = IronicNodeDict.pop("port_info", None)

            ironic_node_sandesh = IronicNode(**IronicNodeDict)
            ironic_node_sandesh.name = IronicNodeDict["name"]

            if DriverInfoDict:
                driver_info = DriverInfo(**DriverInfoDict)
                ironic_node_sandesh.driver_info = driver_info
            if InstanceInfoDict:
                instance_info = InstanceInfo(**InstanceInfoDict)
                ironic_node_sandesh.instance_info = instance_info
            if NodePropertiesDict:
                node_properties = NodeProperties(**NodePropertiesDict)
                ironic_node_sandesh.node_properties = node_properties
            if PortInfoDictList:
                port_list = []
                for PortInfoDict in PortInfoDictList:
                    port_info = PortInfo(**PortInfoDict)
                    port_list.append(port_info)
                ironic_node_sandesh.port_info = port_list

            self._sandesh_logger.info('\nIronic Node Info: %s' %
                                      IronicNodeDict)
            self._sandesh_logger.info('\nIronic Driver Info: %s' %
                                      DriverInfoDict)
            self._sandesh_logger.info('\nIronic Instance Info: %s' %
                                      InstanceInfoDict)
            self._sandesh_logger.info('\nNode Properties: %s' %
                                      NodePropertiesDict)
            self._sandesh_logger.info('\nIronic Port Info: %s' %
                                      PortInfoDictList)

            ironic_sandesh_object = IronicNodeInfo(data=ironic_node_sandesh)
            ironic_sandesh_object.send()

def parse_args(args_str):
    '''
    Eg. python ironic_notification_manager.py \
        -c ironic-notification-manager.conf \
        -c contrail-keystone-auth.conf
    '''

    # Source any specified config/ini file
    # Turn off help, so we      all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'collectors': '127.0.0.1:8086',
        'introspect_port': int(
            ServiceHttpPortMap[ModuleNames[Module.IRONIC_NOTIF_MANAGER]]),
        'log_level': 'SYS_INFO',
        'log_local': False,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY
    }
    ksopts = {
        'auth_host': '127.0.0.1',
        'auth_port': '35357',
        'auth_protocol': 'http',
        'auth_version': 'v2.0',
        'admin_user': '',
        'admin_password': '',
        'admin_tenant_name': '',
        'user_domain_name': None,
        'identity_uri': None,
        'project_domain_name': None,
        'insecure': True,
        'cafile': '',
        'certfile': '',
        'keyfile': '',
        'auth_type': 'password',
        'auth_url': '',
        'region_name': '',
        'endpoint_type': 'internalURL'
    }
    defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
    sandesh_opts = SandeshConfig.get_default_options()

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))
        SandeshConfig.update_options(sandesh_opts, config)

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

    ironic_notification_manager._ironic_kombu_client = IronicKombuClient(
        args.rabbit_server, args.rabbit_port,
        args.rabbit_user, args.rabbit_password,
        args.notification_level, ironic_notification_manager)
    ironic_notification_manager._ironic_kombu_client._start()

# end main


def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()

