#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging

from vnc_api.exceptions import RefsExistError
from vnc_api.vnc_api import (
    AllowedAddressPair,
    AllowedAddressPairs,
    AutonomousSystemsType,
    DefaultProtocolType,
    DeviceChassis,
    DeviceCredential, DeviceCredentialList,
    DeviceFunctionalGroup,
    DnsmasqLeaseParameters,
    EnabledInterfaceParams,
    ExecutableInfoListType, ExecutableInfoType,
    Fabric,
    FatFlowProtocols,
    GlobalSystemConfig,
    HardwareInventory,
    IdPermsType,
    IpAddressesType,
    JobTemplate,
    JunosServicePorts,
    LogicalRouter,
    MacAddressesType, MACLimitControlType, MACMoveLimitControlType,
    PhysicalRouter,
    PlaybookInfoListType, PlaybookInfoType,
    Project,
    ProtocolBgpType, ProtocolOspfType, ProtocolType,
    RouteTargetList,
    RoutingBridgingRolesType,
    RoutingInstance,
    ServiceChainInfo,
    SflowParameters, SflowProfile,
    SNMPCredentials,
    StaticRouteEntriesType, StaticRouteType,
    StatsCollectionFrequency,
    StormControlParameters, StormControlProfile,
    SubnetType,
    TelemetryProfile, TelemetryResourceInfo, TelemetryStateInfo,
    UserCredentials,
    VirtualMachine,
    VirtualMachineInterface,
    VirtualMachineInterfacePropertiesType,
    VirtualNetwork,
    VirtualNetworkRoutedPropertiesType,
    VirtualNetworkType
)

from vnc_cfg_api_server.tests.in_place_upgrade import test_case

logger = logging.getLogger(__name__)


class TestInPlaceUpgradeR2002(test_case.InPlaceUpgradeTestCase):
    def setUp(self, **kwargs):
        super(TestInPlaceUpgradeR2002, self).setUp()
        gsc_fq_name = GlobalSystemConfig().fq_name
        self.gsc = self.api.global_system_config_read(gsc_fq_name)

    @property
    def api(self):
        return self._vnc_lib

    def _project_fetch_or_create(self, test_id):
        project = Project(name='project-{}'.format(test_id))
        try:
            uuid = self.api.project_create(project)
        except RefsExistError:
            uuid = self.api.fq_name_to_id('project', project.fq_name)
        project.set_uuid(uuid)
        return project

    def test_job_template_create(self):
        exe_info_list = ExecutableInfoListType()
        exe_info_list.set_executable_info([
            ExecutableInfoType(executable_path='/tmp/fake',
                               executable_args='a b c',
                               job_completion_weightage=10)])
        playbook_info_list = PlaybookInfoListType()
        playbook_info_list.set_playbook_info([
            PlaybookInfoType(playbook_uri='/tmp/fake/uri/playbook.yaml',
                             multi_device_playbook=False,
                             job_completion_weightage=5)])
        prop_map = {'name': 'jt-{}'.format(self.id()),
                    'parent_obj': self.gsc,
                    'parent_type': 'global-system-config',
                    'display_name': 'job template test',
                    'job_template_executables': exe_info_list,
                    'job_template_output_schema': 'some_string',
                    'job_template_output_ui_schema': 'some_string',
                    'job_template_concurrency_level': 'fabric',
                    'job_template_description': 'test template',
                    'job_template_type': 'workflow',
                    'job_template_input_ui_schema': 'some_string',
                    'job_template_synchronous_job': False,
                    'job_template_input_schema': 'some_string',
                    'job_template_playbooks': playbook_info_list,
                    'annotations': {}}

        obj = self.set_properties(JobTemplate(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_fabric_create(self):
        fabric_credentials = DeviceCredentialList(device_credential=[
            DeviceCredential(
                credential=UserCredentials(username='admin', password='admin'),
                vendor='juniper',
                device_family='juniper')])
        prop_map = {'name': 'f-{}'.format(self.id()),
                    'display_name': 'fabric test',
                    'parent_obj': self.gsc,
                    'parent_type': 'global-system-config',
                    'fabric_ztp': True,
                    'fabric_os_version': 'junos',
                    'fabric_credentials': fabric_credentials,
                    'fabric_enterprise_style': False,
                    'annotations': {}}

        obj = self.set_properties(Fabric(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_device_chassis_create(self):
        prop_map = {'name': 'dc-{}'.format(self.id()),
                    'annotations': {},
                    'device_chassis_type': 'fake_chassis_type',
                    'display_name': 'device chassis'}

        obj = self.set_properties(DeviceChassis(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_device_functional_group_create(self):
        project = self._project_fetch_or_create(self.id())
        bridging_roles = RoutingBridgingRolesType(rb_roles=['CRB', 'ERB'])
        prop_map = {'name': 'dfg-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': project,
                    'parent_type': 'project',
                    'device_functional_group_description': 'some description',
                    'device_functional_group_os_version': 'junos',
                    'device_functional_group_routing_bridging_roles':
                        bridging_roles,
                    'display_name': 'device functional group'}

        obj = self.set_properties(DeviceFunctionalGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_hardware_inventory_create(self):
        default_physical_router = PhysicalRouter()
        try:
            uuid = self.api.physical_router_create(default_physical_router)
        except RefsExistError:
            uuid = self.api.fq_name_to_id('physical_router',
                                          default_physical_router.fq_name)
        default_physical_router.set_uuid(uuid)

        prop_map = {'name': 'hi-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': default_physical_router,
                    'parent_type': 'physical-router',
                    'display_name': 'hardware inventory',
                    'hardware_inventory_inventory_info':
                        '{"id": 123, "name": "fake"}'}

        obj = self.set_properties(HardwareInventory(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_physical_router_create(self):
        dnsmasq = DnsmasqLeaseParameters(
            lease_expiry_time=100,
            client_id='4d972916-3204-11ea-a6fa-d7a3d77d36a2')
        auto_system = AutonomousSystemsType(asn=[294967295])
        junos_service_ports = JunosServicePorts(service_port=['2010'])
        snmp_credentials = SNMPCredentials(
            version=1,
            local_port=8081,
            retries=3,
            timeout=300,
            v2_community='some_string',
            v3_security_name='some_string',
            v3_security_level='some_string',
            v3_security_engine_id='some_string',
            v3_context='some_string',
            v3_context_engine_id='some_string',
            v3_authentication_protocol='some_string',
            v3_authentication_password='some_string',
            v3_privacy_protocol='some_string',
            v3_privacy_password='some_string',
            v3_engine_id='some_string',
            v3_engine_boots=3,
            v3_engine_time=300)
        user_credentials = UserCredentials(username='admin', password='admin')
        routing_bridging_roles = RoutingBridgingRolesType(rb_roles=['CRB'])
        telemetry_info = TelemetryStateInfo(server_ip='10.100.0.100',
                                            server_port='8080',
                                            resource=[TelemetryResourceInfo()])
        prop_map = {'name': 'pr-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': self.gsc,
                    'parent_type': 'global-system-config',
                    'display_name': 'some_string',
                    'physical_router_autonomous_system': auto_system,
                    'physical_router_cli_commit_state': 'in_sync',
                    'physical_router_dataplane_ip': 'some_string',
                    'physical_router_device_family': 'some_string',
                    'physical_router_dhcp_parameters': dnsmasq,
                    'physical_router_encryption_type': 'none',
                    'physical_router_hostname': 'some_string',
                    'physical_router_junos_service_ports': junos_service_ports,
                    'physical_router_lldp': False,
                    'physical_router_loopback_ip': '127.0.0.1',
                    'physical_router_managed_state': 'dhcp',
                    'physical_router_management_ip': '10.100.100.255',
                    'physical_router_management_mac': 'some_string',
                    'physical_router_os_version': 'some_string',
                    'physical_router_product_name': 'some_string',
                    'physical_router_replicator_loopback_ip': 'some_string',
                    'physical_router_role': 'spine',
                    'physical_router_serial_number': 'some_string',
                    'physical_router_snmp': False,
                    'physical_router_snmp_credentials': snmp_credentials,
                    'physical_router_supplemental_config': 'some_string',
                    'physical_router_underlay_config': 'some_string',
                    'physical_router_underlay_managed': False,
                    'physical_router_user_credentials': user_credentials,
                    'physical_router_vendor_name': 'some_string',
                    'physical_router_vnc_managed': False,
                    'routing_bridging_roles': routing_bridging_roles,
                    'telemetry_info': telemetry_info}

        obj = self.set_properties(PhysicalRouter(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_telemetry_profile_create(self):
        project = self._project_fetch_or_create(self.id())
        prop_map = {'name': 'tp-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': project,
                    'parent_type': 'project',
                    'display_name': 'some_string',
                    'telemetry_profile_is_default': False}

        obj = self.set_properties(TelemetryProfile(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_storm_control_profile_create(self):
        project = self._project_fetch_or_create(self.id())
        scp_params = StormControlParameters(
            storm_control_actions=['interface-shutdown'],
            recovery_timeout=30,
            no_unregistered_multicast=True,
            no_registered_multicast=False,
            no_unknown_unicast=False,
            no_multicast=False,
            no_broadcast=False,
            bandwidth_percent=40)
        prop_map = {'name': 'scp-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': project,
                    'parent_type': 'project',
                    'display_name': 'some_string',
                    'storm_control_parameters': scp_params}

        obj = self.set_properties(StormControlProfile(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_sflow_profile_create(self):
        project = self._project_fetch_or_create(self.id())
        stats_collection_freq = StatsCollectionFrequency(sample_rate=42,
                                                         polling_interval=10,
                                                         direction='ingress')
        sfp_params = SflowParameters(
            stats_collection_frequency=stats_collection_freq,
            agent_id='10.100.150.126',
            adaptive_sample_rate=500,
            enabled_interface_type='custom',
            enabled_interface_params=[EnabledInterfaceParams(
                name='default-interface-params',
                stats_collection_frequency=stats_collection_freq,
            )])
        prop_map = {'name': 'sfp-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': project,
                    'parent_type': 'project',
                    'display_name': 'sflow profile',
                    'sflow_parameters': sfp_params,
                    'sflow_profile_is_default': False}

        obj = self.set_properties(SflowProfile(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_routing_instance_create(self):
        project = self._project_fetch_or_create(self.id())
        vn = VirtualNetwork(name='vn-{}'.format(self.id()), parent_obj=project)
        try:
            uuid = self.api.virtual_network_create(vn)
        except RefsExistError:
            uuid = self.api.fq_name_to_id('virtual_network', vn.fq_name)
        vn.set_uuid(uuid)

        ri_name = 'default-routing-instance'
        ri_fq_name = ':'.join(vn.fq_name + [ri_name])

        sci = ServiceChainInfo(
            service_chain_id=ri_fq_name,
            prefix=['20.0.0.0/24'],
            routing_instance=ri_name,
            service_chain_address='0.255.255.250',
            service_instance='default-domain:default-project:test_service',
            sc_head=True)
        sciv6 = ServiceChainInfo(
            service_chain_id=ri_fq_name,
            prefix=['1000::/16'],
            routing_instance=ri_name,
            service_chain_address='::0.255.255.252',
            service_instance='default-domain:default-project:test_service_v6',
            sc_head=False)
        static_route_entries = StaticRouteEntriesType(route=[
            StaticRouteType(prefix='test',
                            next_hop='10.100.100.100',
                            route_target=['test-route-target'],
                            community='')])
        default_ce_protocol = DefaultProtocolType(
            bgp=ProtocolBgpType(autonomous_system=42),
            ospf=ProtocolOspfType(area=1))
        prop_map = {'name': ri_name,
                    'annotations': {},
                    'parent_obj': vn,
                    'parent_type': 'virtual-network',
                    'default_ce_protocol': default_ce_protocol,
                    'display_name': 'some_string',
                    'evpn_ipv6_service_chain_information': sciv6,
                    'evpn_service_chain_information': sci,
                    'ipv6_service_chain_information': sciv6,
                    'routing_instance_fabric_snat': False,
                    'routing_instance_has_pnf': False,
                    'routing_instance_is_default': False,
                    'service_chain_information': sci,
                    'static_route_entries': static_route_entries}

        obj = self.set_properties(RoutingInstance(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_network_create(self):
        project = self._project_fetch_or_create(self.id())
        vn_name = 'vn-{}'.format(self.id())
        rtl = RouteTargetList(route_target=['target:3:1'])
        prop_map = {"name": vn_name,
                    "display_name": vn_name,
                    "fq_name": ['vm' + vn_name],
                    "parent_obj": project,
                    "is_shared": True,
                    "router_external": True,
                    "virtual_network_category": "routed",
                    "port_security_enabled": True,
                    "route_target_list": rtl,
                    "virtual_network_properties": VirtualNetworkType(
                        forwarding_mode='l3'),
                    "address_allocation_mode": 'flat-subnet-only',
                    "mac_learning_enabled": True,
                    "pbb_evpn_enable": True,
                    "pbb_etree_enable": True,
                    "igmp_enable": True,
                    "id_perms": IdPermsType(enable=True),
                    'annotations': {},
                    "mac_aging_time": 400,
                    "fabric_snat": True,
                    "virtual_network_routed_properties":
                        VirtualNetworkRoutedPropertiesType(),
                    "ecmp_hashing_include_fields": False,
                    "provider_properties": None,
                    "flood_unknown_unicast": True,
                    "layer2_control_word": True,
                    "mac_move_control":
                        MACMoveLimitControlType(
                            mac_move_limit=1024,
                            mac_move_limit_action='log',
                            mac_move_time_window=60),
                    "export_route_target_list": rtl,
                    "mac_limit_control":
                        MACLimitControlType(mac_limit=1024,
                                            mac_limit_action='log'),
                    "virtual_network_fat_flow_protocols":
                        FatFlowProtocols([
                            ProtocolType(protocol='p1', port=1),
                            ProtocolType(protocol='p2', port=2)]),
                    "virtual_network_network_id": None,
                    "import_route_target_list": rtl,
                    "external_ipam": True,
                    "multi_policy_service_chains_enabled": False
                    }

        obj = self.set_properties(VirtualNetwork(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_machine_create(self):
        project = self._project_fetch_or_create(self.id())
        vm_name = 'vm-{}'.format(self.id())
        prop_map = {
            "name": vm_name,
            "parent_obj": project,
            "display_name": 'vm' + vm_name,
            "fq_name": ['vm' + vm_name],
            "server_type": 'baremetal-server',
            "id_perms": IdPermsType(enable=True),
            'annotations': {}
        }

        obj = self.set_properties(VirtualMachine(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_logical_router_create(self):
        project = self._project_fetch_or_create(self.id())
        lr_name = 'lr-{}'.format(self.id())
        ip = IpAddressesType(["1.1.1.1"])
        prop_map = {
            "name": lr_name,
            "parent_obj": project,
            "display_name": lr_name,
            "fq_name": ['lr' + lr_name],
            "id_perms": IdPermsType(enable=True),
            "logical_router_gateway_external": False,
            "logical_router_type": 'vxlan-routing',
            "vxlan_network_identifier": '1111',
            "configured_route_target_list": None,
            "logical_router_dhcp_relay_server": ip,
            'annotations': {}
        }

        obj = self.set_properties(LogicalRouter(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)
