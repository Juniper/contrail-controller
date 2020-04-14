#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved
#

# flake8: noqa

from builtins import str as builtin_str
from future.utils import native_str

from cfgm_common.utils import str_to_class
from vnc_api.gen import resource_common
from vnc_api.gen.vnc_api_client_gen import all_resource_type_tuples

from vnc_cfg_api_server.resources._resource_base import ResourceMixin
from vnc_cfg_api_server.resources.address_group import AddressGroupServer
from vnc_cfg_api_server.resources.alarm import AlarmServer
from vnc_cfg_api_server.resources.alias_ip import AliasIpServer
from vnc_cfg_api_server.resources.application_policy_set import\
   ApplicationPolicySetServer
from vnc_cfg_api_server.resources.bgp_as_a_service import BgpAsAServiceServer
from vnc_cfg_api_server.resources.bgp_router import BgpRouterServer
from vnc_cfg_api_server.resources.bgpvpn import BgpvpnServer
from vnc_cfg_api_server.resources.bridge_domain import BridgeDomainServer
from vnc_cfg_api_server.resources.data_center_interconnect import\
    DataCenterInterconnectServer
from vnc_cfg_api_server.resources.domain import DomainServer
from vnc_cfg_api_server.resources.firewall_policy import FirewallPolicyServer
from vnc_cfg_api_server.resources.firewall_rule import FirewallRuleServer
from vnc_cfg_api_server.resources.floating_ip import FloatingIpServer
from vnc_cfg_api_server.resources.floating_ip_pool import FloatingIpPoolServer
from vnc_cfg_api_server.resources.forwarding_class import ForwardingClassServer
from vnc_cfg_api_server.resources.global_system_config import\
    GlobalSystemConfigServer
from vnc_cfg_api_server.resources.host_based_service import\
    HostBasedServiceServer
from vnc_cfg_api_server.resources.instance_ip import InstanceIpServer
from vnc_cfg_api_server.resources.logical_interface import\
    LogicalInterfaceServer
from vnc_cfg_api_server.resources.logical_router import LogicalRouterServer
from vnc_cfg_api_server.resources.network_ipam import NetworkIpamServer
from vnc_cfg_api_server.resources.network_policy import NetworkPolicyServer
from vnc_cfg_api_server.resources.physical_interface import\
    PhysicalInterfaceServer
from vnc_cfg_api_server.resources.physical_router import PhysicalRouterServer
from vnc_cfg_api_server.resources.fabric import FabricServer
from vnc_cfg_api_server.resources.policy_management import\
    PolicyManagementServer
from vnc_cfg_api_server.resources.project import ProjectServer
from vnc_cfg_api_server.resources.qos_config import QosConfigServer
from vnc_cfg_api_server.resources.route_aggregate import RouteAggregateServer
from vnc_cfg_api_server.resources.route_table import RouteTableServer
from vnc_cfg_api_server.resources.route_target import RouteTargetServer
from vnc_cfg_api_server.resources.routing_instance import RoutingInstanceServer
from vnc_cfg_api_server.resources.routing_policy import RoutingPolicyServer
from vnc_cfg_api_server.resources.security_group import SecurityGroupServer
from vnc_cfg_api_server.resources.service_appliance_set import\
    ServiceApplianceSetServer
from vnc_cfg_api_server.resources.storm_control_profile import\
    StormControlProfileServer
from vnc_cfg_api_server.resources.port_profile import PortProfileServer
from vnc_cfg_api_server.resources.service_group import ServiceGroupServer
from vnc_cfg_api_server.resources.service_template import ServiceTemplateServer
from vnc_cfg_api_server.resources.sflow_profile import SflowProfileServer
from vnc_cfg_api_server.resources.sub_cluster import SubClusterServer
from vnc_cfg_api_server.resources.tag import TagServer
from vnc_cfg_api_server.resources.tag_type import TagTypeServer
from vnc_cfg_api_server.resources.telemetry_profile import\
    TelemetryProfileServer
from vnc_cfg_api_server.resources.virtual_dns import VirtualDnsServer
from vnc_cfg_api_server.resources.virtual_dns_record import\
    VirtualDnsRecordServer
from vnc_cfg_api_server.resources.virtual_machine_interface import\
    VirtualMachineInterfaceServer
from vnc_cfg_api_server.resources.virtual_network import VirtualNetworkServer
from vnc_cfg_api_server.resources.virtual_router import VirtualRouterServer
from vnc_cfg_api_server.resources.virtual_port_group import\
    VirtualPortGroupServer
from vnc_cfg_api_server.resources.service_appliance import\
    ServiceApplianceServer
from vnc_cfg_api_server.resources.service_instance import ServiceInstanceServer
from vnc_cfg_api_server.resources.port_tuple import PortTupleServer
from vnc_cfg_api_server.resources.node_port import PortServer
from vnc_cfg_api_server.resources.node import NodeServer
from vnc_cfg_api_server.resources.fabric_namespace import FabricNamespaceServer

def initialize_all_server_resource_classes(server_instance):
    """Initialize map of all resource server classes.

    Initialize with the API server instance a class for all Contrail resource
    types with generated common resource class in vnc_api lib and the server
    resource class mixin which overloads common classes with all necessary
    hooks for CRUD API calls. These hooks will be empty if they the resource
    class does not defines its server class in that module.

    :param server_instance:
        `vnc_cfg_api_server.api_server.VncApiServer` instance
    :returns: map of server resource class with the resource type as key (in
        both formats ('-' and '_') if resource have multiple word in its name
    """
    server_class_map = {}
    for object_type, resource_type in all_resource_type_tuples:
        common_name = object_type.title().replace('_', '')
        server_name = '%sServer' % common_name
        server_class = str_to_class(server_name, __name__)
        if not server_class:
            common_class = str_to_class(common_name, resource_common.__name__)
            # Create Placeholder classes derived from ResourceMixin, <Type> so
            # resource server class methods can be invoked in CRUD methods
            # without checking for None
            server_class = type(
                native_str(server_name),
                (ResourceMixin, common_class, object),
                {})
        server_class.server = server_instance
        server_class_map[object_type] = server_class
        server_class_map[resource_type] = server_class
    return server_class_map
