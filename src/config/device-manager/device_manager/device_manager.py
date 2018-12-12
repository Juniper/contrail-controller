#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of managing physical router configuration
"""

import gevent
# Import kazoo.client before monkey patching
from gevent import monkey
monkey.patch_all()
import requests
import ConfigParser
import time
import hashlib
import signal
import random
import traceback

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.vnc_db import DBBase
from vnc_api.vnc_api import VncApi
from db import DBBaseDM, BgpRouterDM, PhysicalRouterDM, PhysicalInterfaceDM,\
    ServiceInstanceDM, LogicalInterfaceDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM, RoutingInstanceDM, GlobalSystemConfigDM, LogicalRouterDM, \
    GlobalVRouterConfigDM, FloatingIpDM, InstanceIpDM, PortTupleDM, \
    ServiceEndpointDM, ServiceConnectionModuleDM, ServiceObjectDM, \
    NetworkDeviceConfigDM, E2ServiceProviderDM, PeeringPolicyDM, \
    SecurityGroupDM, AccessControlListDM, NodeProfileDM, FabricNamespaceDM, \
    RoleConfigDM, FabricDM, LinkAggregationGroupDM, FloatingIpPoolDM, \
    DataCenterInterconnectDM
from dm_amqp import DMAmqpHandle
from dm_utils import PushConfigState
from ansible_base import AnsibleBase
from device_conf import DeviceConf
from logger import DeviceManagerLogger

# zookeeper client connection
_zookeeper_client = None


class DeviceManager(object):
    REACTION_MAP = {
        'physical_router': {
            'self': ['bgp_router',
                     'physical_interface',
                     'logical_interface',
                     'e2_service_provider',
                     'service_endpoint'],
            'bgp_router': [],
            'physical_interface': [],
            'logical_interface': [],
            'virtual_network': [],
            'logical_router': [],
            'global_system_config': [],
            'network_device_config': [],
            'service_endpoint': [],
            'service_connection_module': [],
            'service_object': [],
            'e2_service_provider': [],
            'node_profile': [],
            'role_config': [],
            'fabric': [],
            'fabric_namespace': [],
            'link_aggregation_group': [],
        },
        'global_system_config': {
            'self': ['physical_router', 'data_center_interconnect'],
            'physical_router': [],
        },
        'node_profile': {
            'self': ['physical_router'],
            'role_config': ['physical_router'],
        },
        'data_center_interconnect': {
            'self': ['logical_router', 'virtual_network'],
            'logical_router': [],
            'virtual_network': ['logical_router'],
            'global_system_config': ['logical_router'],
        },
        'role_config': {
            'self': ['node_profile'],
            'node_profile': [],
        },
        'link_aggregation_group': {
            'self': ['physical_router'],
            'physical_router': [],
        },
        'fabric': {
            'self': ['physical_router'],
            'fabric_namespace': ['physical_router'],
        },
        'fabric_namespace': {
            'self': ['fabric'],
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
                     'virtual_machine_interface',
                     'service_endpoint'],
            'physical_interface': ['virtual_machine_interface'],
            'virtual_machine_interface': ['physical_router',
                                          'physical_interface'],
            'physical_router': ['virtual_machine_interface'],
            'instance_ip': ['physical_interface'],
        },
        'virtual_machine_interface': {
            'self': ['logical_interface',
                     'physical_interface',
                     'virtual_network',
                     'logical_router',
                     'floating_ip',
                     'instance_ip',
                     'port_tuple',
                     'service_endpoint'],
            'logical_interface': ['virtual_network'],
            'virtual_network': ['logical_interface', 'logical_router'],
            'logical_router': [],
            'floating_ip': ['virtual_network'],
            'instance_ip': ['virtual_network'],
            'routing_instance': ['port_tuple', 'physical_interface'],
            'port_tuple': ['physical_interface'],
            'service_endpoint': ['physical_router'],
            'security_group': ['logical_interface'],
        },
        'security_group': {
            'self': [],
            'access_control_list': ['virtual_machine_interface'],
        },
        'access_control_list': {
            'self': ['security_group'],
            'security_group': [],
        },
        'service_instance': {
            'self': ['port_tuple'],
            'port_tuple': [],
        },
        'port_tuple': {
            'self': ['virtual_machine_interface', 'service_instance'],
            'service_instance': ['virtual_machine_interface'],
            'virtual_machine_interface': ['service_instance']
        },
        'virtual_network': {
            'self': ['physical_router', 'data_center_interconnect',
                     'virtual_machine_interface', 'logical_router', 'fabric', 'floating_ip_pool'],
            'routing_instance': ['physical_router', 'logical_router',
                                 'virtual_machine_interface'],
            'physical_router': [],
            'logical_router': ['physical_router'],
            'data_center_interconnect': ['physical_router'],
            'virtual_machine_interface': ['physical_router'],
            'floating_ip_pool': ['physical_router'],
        },
        'logical_router': {
            'self': ['physical_router', 'virtual_network'],
            'physical_router': [],
            'data_center_interconnect': ['physical_router'],
            'virtual_network': ['physical_router'],
            'routing_instance': ['physical_router'],
            'virtual_machine_interface': ['physical_router']
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
            'self': ['virtual_machine_interface', 'floating_ip_pool'],
            'virtual_machine_interface': ['floating_ip_pool'],
            'floating_ip_pool': ['virtual_machine_interface']
        },
        'floating_ip_pool': {
            'self': ['virtual_network'],
            'virtual_network': ['floating_ip'],
            'floating_ip': ['virtual_network']
        },
        'instance_ip': {
            'self': ['virtual_machine_interface', 'logical_interface'],
            'virtual_machine_interface': [],
        },
        'service_endpoint': {
            'self': ['physical_router',
                     'virtual_machine_interface',
                     'service_connection_module'],
            'physical_router': ['service_connection_module'],
            'logical_interface': ['service_connection_module'],
            'virtual_machine_interface': ['service_connection_module'],
            'service_connection_module': [],
        },
        'service_connection_module': {
            'self': ['service_endpoint'],
            'service_endpoint': [],
        },
        'service_object': {
            'self': [],
        },
        'network_device_config': {
            'self': [],
        },
        'e2_service_provider': {
            'self': ['physical_router',
                     'peering_policy'],
            'physical_router': [],
            'peering_policy': [],
        },
        'peering_policy': {
            'self': ['e2_service_provider'],
            'e2_service_provider': [],
        },
    }

    _instance = None

    def __init__(self, dm_logger=None, args=None, object_db=None,
                 amqp_client=None):
        DeviceManager._instance = self
        self._args = args
        self._amqp_client = amqp_client
        self._object_db = object_db

        PushConfigState.set_push_mode(int(self._args.push_mode))
        PushConfigState.set_repush_interval(int(self._args.repush_interval))
        PushConfigState.set_repush_max_interval(
            int(self._args.repush_max_interval))
        PushConfigState.set_push_delay_per_kb(
            float(self._args.push_delay_per_kb))
        PushConfigState.set_push_delay_max(int(self._args.push_delay_max))
        PushConfigState.set_push_delay_enable(
            bool(self._args.push_delay_enable))

        self._chksum = ""
        if self._args.collectors:
            self._chksum = hashlib.md5(
                ''.join(self._args.collectors)).hexdigest()

        # Initialize logger
        self.logger = dm_logger or DeviceManagerLogger(args)

        # Register Plugins
        try:
            DeviceConf.register_plugins()
        except DeviceConf.PluginsRegistrationFailed as e:
            self.logger.error("Exception: " + str(e))
        except Exception as e:
            tb = traceback.format_exc()
            self.logger.error(
                "Internal error while registering plugins: " + str(e) + tb)

        # Register Ansible Plugins
        try:
            AnsibleBase.register_plugins()
        except AnsibleBase.PluginsRegistrationFailed as e:
            self.logger.error("Exception: " + str(e))
        except Exception as e:
            tb = traceback.format_exc()
            self.logger.error(
                "Internal error while registering ansible plugins: " +
                str(e) + tb)

        # Retry till API server is up
        connected = False
        self.connection_state_update(ConnectionStatus.INIT)
        api_server_list = args.api_server_ip.split(',')
        while not connected:
            try:
                self._vnc_lib = VncApi(
                    args.admin_user, args.admin_password,
                    args.admin_tenant_name, api_server_list,
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

        # Initialize amqp
        self._vnc_amqp = DMAmqpHandle(self.logger, self.REACTION_MAP,
                                      self._args)
        self._vnc_amqp.establish()

        DBBaseDM.init(self, self.logger, self._object_db)
        DBBaseDM._sandesh = self.logger._sandesh

        for obj in GlobalSystemConfigDM.list_obj():
            GlobalSystemConfigDM.locate(obj['uuid'], obj)

        for obj in NodeProfileDM.list_obj():
            NodeProfileDM.locate(obj['uuid'], obj)

        for obj in RoleConfigDM.list_obj():
            RoleConfigDM.locate(obj['uuid'], obj)

        for obj in GlobalVRouterConfigDM.list_obj():
            GlobalVRouterConfigDM.locate(obj['uuid'], obj)

        for obj in VirtualNetworkDM.list_obj():
            VirtualNetworkDM.locate(obj['uuid'], obj)

        dci_obj_list = DataCenterInterconnectDM.list_obj()
        for obj in dci_obj_list or []:
            DataCenterInterconnectDM.locate(obj['uuid'], obj)

        for obj in FabricDM.list_obj():
            FabricDM.locate(obj['uuid'], obj)

        for obj in FabricNamespaceDM.list_obj():
            FabricNamespaceDM.locate(obj['uuid'], obj)

        for obj in LogicalRouterDM.list_obj():
            LogicalRouterDM.locate(obj['uuid'], obj)

        for obj in RoutingInstanceDM.list_obj():
            RoutingInstanceDM.locate(obj['uuid'], obj)

        for obj in FloatingIpPoolDM.list_obj():
            FloatingIpPoolDM.locate(obj['uuid'], obj)

        for obj in BgpRouterDM.list_obj():
            BgpRouterDM.locate(obj['uuid'], obj)

        for obj in PortTupleDM.list_obj():
            PortTupleDM.locate(obj['uuid'], obj)

        for obj in PhysicalInterfaceDM.list_obj():
            PhysicalInterfaceDM.locate(obj['uuid'], obj)

        for obj in LinkAggregationGroupDM.list_obj():
            LinkAggregationGroupDM.locate(obj['uuid'], obj)

        for obj in LogicalInterfaceDM.list_obj():
            LogicalInterfaceDM.locate(obj['uuid'], obj)

        pr_obj_list = PhysicalRouterDM.list_obj()
        for obj in pr_obj_list:
            PhysicalRouterDM.locate(obj['uuid'], obj)

        pr_uuid_set = set([pr_obj['uuid'] for pr_obj in pr_obj_list])
        self._object_db.handle_pr_deletes(pr_uuid_set)

        dci_uuid_set = set([dci_obj['uuid'] for dci_obj in dci_obj_list])
        self._object_db.handle_dci_deletes(dci_uuid_set)

        for obj in VirtualMachineInterfaceDM.list_obj():
            VirtualMachineInterfaceDM.locate(obj['uuid'], obj)

        for obj in SecurityGroupDM.list_obj():
            SecurityGroupDM.locate(obj['uuid'], obj)

        for obj in AccessControlListDM.list_obj():
            AccessControlListDM.locate(obj['uuid'], obj)

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

        for obj in ServiceEndpointDM.list_obj():
            ServiceEndpointDM.locate(obj['uuid'], obj)

        for obj in ServiceConnectionModuleDM.list_obj():
            ServiceConnectionModuleDM.locate(obj['uuid'], obj)

        for obj in ServiceObjectDM.list_obj():
            ServiceObjectDM.locate(obj['uuid'], obj)

        for obj in NetworkDeviceConfigDM.list_obj():
            NetworkDeviceConfigDM.locate(obj['uuid'], obj)

        for obj in E2ServiceProviderDM.list_obj():
            E2ServiceProviderDM.locate(obj['uuid'], obj)

        for obj in PeeringPolicyDM.list_obj():
            PeeringPolicyDM.locate(obj['uuid'], obj)

        for pr in PhysicalRouterDM.values():
            pr.set_config_state()
            pr.uve_send()

        self._vnc_amqp._db_resync_done.set()

        gevent.joinall(self._vnc_amqp._vnc_kombu.greenlets())
    # end __init__

    def get_analytics_config(self):
        return {
            'ips': self._args.analytics_server_ip.split(','),
            'port': self._args.analytics_server_port,
            'username': self._args.analytics_username,
            'password': self._args.analytics_password
        }
    # end get_analytics_config

    def get_api_server_config(self):
        return {
            'ips': self._args.api_server_ip.split(','),
            'port': self._args.api_server_port,
            'username': self._args.admin_user,
            'password': self._args.admin_password,
            'tenant': self._args.admin_tenant_name,
            'use_ssl': self._args.api_server_use_ssl
        }
    # end get_api_server_config

    def get_job_status_config(self):
        return {
            'timeout': int(self._args.job_status_retry_timeout),
            'max_retries': int(self._args.job_status_max_retries)
        }
    # end get_job_status_config

    @classmethod
    def get_instance(cls):
        return cls._instance
     # end get_instance

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if not inst:
            return
        inst._vnc_amqp.close()
        for obj_cls in DBBaseDM.get_obj_type_map().values():
            obj_cls.reset()
        DBBase.clear()
        cls._instance = None
    # end destroy_instance

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
                       # Reconnect to achieve load-balance irrespective of list
                       self.logger.sandesh_reconfig_collectors(config)
               except ConfigParser.NoOptionError as _:
                   pass
    # end sighup_handler
