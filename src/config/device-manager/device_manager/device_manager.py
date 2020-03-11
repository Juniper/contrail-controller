#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
Physical router configuration implementation.

This file contains implementation of managing physical router configuration
based on intent configuration.
"""
from __future__ import absolute_import
from future import standard_library # noqa
standard_library.install_aliases() # noqa

from builtins import object # noqa
from builtins import str
import ConfigParser as configparser
import hashlib
import random
import time
import traceback

from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.vnc_db import DBBase
import gevent
from gevent import monkey
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
# Import kazoo.client before monkey patching
import requests
from vnc_api.vnc_api import VncApi

from .ansible_base import AnsibleBase
from .db import AccessControlListDM, BgpRouterDM, DataCenterInterconnectDM, \
    DBBaseDM, DMCassandraDB, E2ServiceProviderDM, FabricDM, \
    FabricNamespaceDM, FeatureConfigDM, FeatureDM, FloatingIpDM, \
    FloatingIpPoolDM, FlowNodeDM, GlobalSystemConfigDM, \
    GlobalVRouterConfigDM, \
    InstanceIpDM, IntentMapDM, InterfaceRouteTableDM, \
    LinkAggregationGroupDM, LogicalInterfaceDM, \
    LogicalRouterDM, NetworkDeviceConfigDM, NetworkIpamDM, NodeProfileDM, \
    OverlayRoleDM, PeeringPolicyDM, PhysicalInterfaceDM, PhysicalRoleDM, \
    PhysicalRouterDM, PortDM, PortProfileDM, PortTupleDM, RoleConfigDM, \
    RoleDefinitionDM, RoutingInstanceDM, RoutingPolicyDM, SecurityGroupDM, \
    ServiceApplianceDM, ServiceApplianceSetDM, ServiceConnectionModuleDM, \
    ServiceEndpointDM, ServiceInstanceDM, ServiceObjectDM, ServiceTemplateDM, \
    SflowProfileDM, StormControlProfileDM, TagDM, TelemetryProfileDM, \
    VirtualMachineInterfaceDM, VirtualNetworkDM, VirtualPortGroupDM
from .device_conf import DeviceConf
from .dm_amqp import DMAmqpHandle
from .dm_utils import PushConfigState
from .fabric_manager import FabricManager
from .feature_base import FeatureBase
from .logger import DeviceManagerLogger
monkey.patch_all() # noqa


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
            'telemetry_profile': [],
            'role_config': [],
            'fabric': [],
            'fabric_namespace': [],
            'virtual_port_group': [],
            'service_instance': [],
            'service_appliance': [],
            'intent_map': [],
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
        'virtual_port_group': {
            'self': ['physical_interface'],
            'virtual_machine_interface': ['physical_interface'],
            'physical_interface': ['physical_interface'],
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
                     'logical_interface',
                     'virtual_port_group'
                     ],
            'physical_router': ['logical_interface'],
            'logical_interface': ['physical_interface', 'physical_router'],
            'physical_interface': ['physical_router'],
            'virtual_port_group': ['physical_router'],
            'virtual_machine_interface': ['physical_interface',
                                          'physical_router'],
            'service_appliance': ['physical_router'],
            'port': ['physical_router'],
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
        'service_appliance': {
            'self': ['physical_interface'],
            'service_appliance_set': ['physical_interface'],
        },
        'virtual_machine_interface': {
            'self': ['logical_interface',
                     'physical_interface',
                     'virtual_network',
                     'logical_router',
                     'floating_ip',
                     'instance_ip',
                     'port_tuple',
                     'service_endpoint',
                     'virtual_port_group'],
            'logical_interface': ['virtual_network'],
            'virtual_network': ['logical_interface', 'logical_router',
                                'intent_map'],
            'logical_router': [],
            'floating_ip': ['virtual_network'],
            'instance_ip': ['virtual_network'],
            'routing_instance': ['port_tuple', 'physical_interface'],
            'port_tuple': ['physical_interface'],
            'service_endpoint': ['physical_router'],
            'security_group': ['logical_interface', 'virtual_port_group'],
            'port_profile': ['virtual_port_group'],
            'interface_route_table': ['logical_router'],
        },
        'security_group': {
            'self': [],
            'access_control_list': ['virtual_machine_interface'],
        },
        'access_control_list': {
            'self': ['security_group'],
            'security_group': [],
        },
        'port_profile': {
            'self': ['virtual_machine_interface'],
            'storm_control_profile': ['virtual_machine_interface'],
        },
        'storm_control_profile': {
            'self': ['port_profile'],
        },
        'telemetry_profile': {
            'self': ['physical_router'],
            'sflow_profile': ['physical_router'],
        },
        'sflow_profile': {
            'self': ['telemetry_profile'],
        },
        'service_appliance_set': {
            'self': [],
            'service_template': ['service_appliance'],
        },
        'service_template': {
            'self': [],
            'service_instance': ['service_appliance_set'],
        },
        'service_instance': {
            'self': ['port_tuple'],
            'port_tuple': ['service_template'],
        },
        'port_tuple': {
            'self': ['virtual_machine_interface', 'service_instance'],
            'logical_router': ['service_instance'],
            'service_instance': ['virtual_machine_interface',
                                 'service_template'],
            'virtual_machine_interface': ['service_instance']
        },
        'virtual_network': {
            'self': ['physical_router', 'data_center_interconnect',
                     'virtual_machine_interface', 'logical_router',
                     'fabric', 'floating_ip_pool', 'tag', 'network_ipam'],
            'routing_instance': ['physical_router', 'logical_router',
                                 'virtual_machine_interface'],
            'physical_router': [],
            'logical_router': ['physical_router'],
            'data_center_interconnect': ['physical_router'],
            'virtual_machine_interface': ['physical_router', 'intent_map'],
            'floating_ip_pool': ['physical_router'],
            'network_ipam': ['tag'],
            'routing_policy': ['virtual_machine_interface'],
            'intent_map': ['physical_router'],
        },
        'logical_router': {
            'self': ['physical_router', 'virtual_network', 'port_tuple'],
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
        'tag': {
            'self': ['port'],
            'virtual_network': ['port']
        },
        'network_ipam': {
            'self': ['virtual_network'],
            'virtual_network': ['tag']
        },
        'port': {
            'self': ['physical_interface'],
            'tag': ['physical_interface'],
        },
        'interface_route_table': {
            'self': ['virtual_machine_interface'],
        },
        'routing_policy': {
            'self': ['virtual_network'],
        },
        'intent_map': {
            'self': ['physical_router'],
            'virtual_network': ['physical_router'],
            'virtual_machine_interface': ['physical_router']
        }
    }

    _instance = None

    def __init__(self, dm_logger=None, args=None, zookeeper_client=None,
                 amqp_client=None):
        """Physical Router init routine."""
        DeviceManager._instance = self
        self._args = args
        self._amqp_client = amqp_client
        self.logger = dm_logger or DeviceManagerLogger(args)
        self._vnc_amqp = DMAmqpHandle(self.logger, self.REACTION_MAP,
                                      self._args)

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

        # Register Feature Plugins
        try:
            FeatureBase.register_plugins()
        except FeatureBase.PluginRegistrationFailed as e:
            self.logger.error("Exception: " + str(e))
        except Exception as e:
            tb = traceback.format_exc()
            self.logger.error(
                "Internal error while registering feature plugins: " +
                str(e) + tb)
            raise e

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

        if PushConfigState.is_push_mode_ansible():
            FabricManager.initialize(args, dm_logger, self._vnc_lib)
        # Initialize amqp
        self._vnc_amqp.establish()

        # Initialize cassandra
        self._object_db = DMCassandraDB.get_instance(zookeeper_client,
                                                     self._args, self.logger)
        DBBaseDM.init(self, self.logger, self._object_db)
        DBBaseDM._sandesh = self.logger._sandesh

        GlobalSystemConfigDM.locate_all()
        FlowNodeDM.locate_all()
        FeatureDM.locate_all()
        PhysicalRoleDM.locate_all()
        OverlayRoleDM.locate_all()
        RoleDefinitionDM.locate_all()
        FeatureConfigDM.locate_all()
        NodeProfileDM.locate_all()
        RoleConfigDM.locate_all()
        GlobalVRouterConfigDM.locate_all()
        VirtualNetworkDM.locate_all()
        DataCenterInterconnectDM.locate_all()
        FabricDM.locate_all()
        FabricNamespaceDM.locate_all()
        LogicalRouterDM.locate_all()
        RoutingInstanceDM.locate_all()
        FloatingIpPoolDM.locate_all()
        BgpRouterDM.locate_all()
        PhysicalInterfaceDM.locate_all()
        LogicalInterfaceDM.locate_all()
        IntentMapDM.locate_all()
        PhysicalRouterDM.locate_all()
        LinkAggregationGroupDM.locate_all()
        VirtualPortGroupDM.locate_all()
        PortDM.locate_all()
        TagDM.locate_all()
        NetworkIpamDM.locate_all()
        VirtualMachineInterfaceDM.locate_all()
        SecurityGroupDM.locate_all()
        AccessControlListDM.locate_all()
        PortProfileDM.locate_all()
        StormControlProfileDM.locate_all()
        TelemetryProfileDM.locate_all()
        SflowProfileDM.locate_all()
        ServiceInstanceDM.locate_all()
        ServiceApplianceSetDM.locate_all()
        ServiceApplianceDM.locate_all()
        ServiceTemplateDM.locate_all()
        PortTupleDM.locate_all()
        InstanceIpDM.locate_all()
        FloatingIpDM.locate_all()

        for vn in list(VirtualNetworkDM.values()):
            vn.update_instance_ip_map()

        ServiceEndpointDM.locate_all()
        ServiceConnectionModuleDM.locate_all()
        ServiceObjectDM.locate_all()
        NetworkDeviceConfigDM.locate_all()
        E2ServiceProviderDM.locate_all()
        PeeringPolicyDM.locate_all()
        InterfaceRouteTableDM.locate_all()
        RoutingPolicyDM.locate_all()

        pr_obj_list = PhysicalRouterDM.list_obj()
        pr_uuid_set = set([pr_obj['uuid'] for pr_obj in pr_obj_list])
        self._object_db.handle_pr_deletes(pr_uuid_set)

        dci_obj_list = DataCenterInterconnectDM.list_obj()
        dci_uuid_set = set([dci_obj['uuid'] for dci_obj in dci_obj_list])
        self._object_db.handle_dci_deletes(dci_uuid_set)

        si_obj_list = ServiceInstanceDM.list_obj()
        si_uuid_set = set([si_obj['uuid'] for si_obj in si_obj_list])
        self._object_db.handle_pnf_resource_deletes(si_uuid_set)

        for pr in list(PhysicalRouterDM.values()):
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
        if PushConfigState.is_push_mode_ansible():
            FabricManager.destroy_instance()
        for obj_cls in list(DBBaseDM.get_obj_type_map().values()):
            obj_cls.reset()
        DBBase.clear()
        DMCassandraDB.clear_instance()
        cls._instance = None
    # end destroy_instance

    def connection_state_update(self, status, message=None):
        ConnectionState.update(
            conn_type=ConnType.APISERVER, name='ApiServer',
            status=status,
            message=message or 'ApiServer Connection State updated',
            server_addrs=['%s:%s' % (self._args.api_server_ip,
                                     self._args.api_server_port)])
    # end connection_state_update

    # sighup handler for applying new configs
    def sighup_handler(self):
        if self._args.conf_file:
            config = configparser.SafeConfigParser()
            config.read(self._args.conf_file)
            if 'DEFAULTS' in config.sections():
                try:
                    collectors = config.get('DEFAULTS', 'collectors')
                    if type(collectors) is str:
                        collectors = collectors.split()
                        new_chksum = hashlib.md5(
                            "".join(collectors)).hexdigest()
                        if new_chksum != self._chksum:
                            self._chksum = new_chksum
                            config.random_collectors = random.sample(
                                collectors, len(collectors))
                        # Reconnect to achieve loadbalance irrespective of list
                        self.logger.sandesh_reconfig_collectors(config)
                except configparser.NoOptionError:
                    pass
    # end sighup_handler
