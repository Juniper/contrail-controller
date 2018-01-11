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

"""
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
    VirtualNetworkDM, RoutingInstanceDM, GlobalSystemConfigDM, LogicalRouterDM, \
    GlobalVRouterConfigDM, FloatingIpDM, InstanceIpDM, DMCassandraDB, PortTupleDM, \
    ServiceEndpointDM, ServiceConnectionModuleDM, ServiceObjectDM, \
    NetworkDeviceConfigDM, E2ServiceProviderDM, PeeringPolicyDM
from dm_amqp import DMAmqpHandle
from dm_utils import PushConfigState
from device_conf import DeviceConf
from cfgm_common.dependency_tracker import DependencyTracker
from cfgm_common import vnc_cgitb
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_logger import ConfigServiceLogger
from logger import DeviceManagerLogger

"""



"""
from contrail_sm_monitoring.monitoring.ttypes import *
from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames, \
    Module2NodeType, INSTANCE_ID_DEFAULT
from sandesh_common.vns.constants import *
"""

from ironic_kombu import IronicKombuClient

class IronicNotificationManager(object):

    _ironic_notification_manager = None
    _ironic_kombu_client = None

    def __init__(self, inm_logger=None, args=None):
        self._args = args

        IronicNotificationManager._ironic_notification_manager = self
    # end __init__

    def sync_with_ironic(self, auth_server, auth_port,
        auth_protocol, auth_version,
        admin_user, admin_password, admin_tenant_name,
        ironic_server_ip, ironic_server_port):

        auth_url = '%s://%s:%s/%s' % (auth_protocol, auth_server, auth_port, auth_version)

        print auth_url

        httpclient = HTTPClient(username=admin_user, project_name=admin_tenant_name,
            password=admin_password, auth_url=auth_url)

        httpclient.authenticate()

        ironic_url = 'http://%s:%s/v1/nodes/detail' % (ironic_server_ip, ironic_server_port)
        auth_token = httpclient.auth_token
        headers = {'X-Auth-Token': str(auth_token)}

        resp = requests.get(ironic_url, headers=headers)

        resp_dict = resp.json()
        node_dict_list = resp_dict["nodes"]
        self.process_ironic_node_info(node_dict_list)

    def process_ironic_node_info(self, node_dict_list):

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

            for key in IronicNodeDictKeyMap.keys():
                if key in node_dict and key not in sub_dict_list:
                    IronicNodeDict[IronicNodeDictKeyMap[key]] = node_dict[key]

            for sub_dict in sub_dict_list:
                IronicNodeDict[sub_dict] = {}
                if sub_dict in node_dict.keys():
                    for key in node_dict[sub_dict]:
                        IronicNodeDict[sub_dict][key] = node_dict[sub_dict][key]

            DriverInfoDict = IronicNodeDict.pop("driver_info",{})
            InstanceInfoDict = IronicNodeDict.pop("instance_info",{}) 
            NodePropertiesDict = IronicNodeDict.pop("properties",{})

            pp = pprint.PrettyPrinter(indent=4)
            print "\nIronic Node Info:"
            pp.pprint(IronicNodeDict)
            print "\nIronic Driver Info:"
            pp.pprint(DriverInfoDict)
            print "\nIronic Instance Info:"
            pp.pprint(InstanceInfoDict)
            print "\nNode Properties Info:"
            pp.pprint(NodePropertiesDict)

def parse_args(args_str):
    '''
    Eg. python ironic_notification_manager.py
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

    saved_conf_file = args.conf_file
    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))

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
    parser.set_defaults(**defaults)

    args = parser.parse_args(remaining_argv)
    args.conf_file = saved_conf_file
    return args
# end parse_args

def main(args_str=None):

    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    ironic_notification_manager = IronicNotificationManager(args=args)

    ironic_notification_manager.sync_with_ironic(
        args.auth_server, args.auth_port,
        args.auth_protocol, args.auth_version, 
        args.admin_user, args.admin_password, args.admin_tenant_name,
        args.ironic_server_ip, args.ironic_server_port)

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
