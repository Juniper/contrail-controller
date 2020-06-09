#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging

from vnc_api.exceptions import RefsExistError
from vnc_api.vnc_api import *

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
                    'disable_vlan_vn_uniqueness_check': False,
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

    def test_control_node_zone_create(self):
        prop_map = {
            'name': 'cnz-{}'.format(self.id()),
            'parent_obj': self.gsc,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ControlNodeZone(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_endpoint_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'service endpoint name'
        }
        obj = self.set_properties(ServiceEndpoint(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_instance_ip_create(self):
        prop_map = {
            'instance_ip_address': '10.100.0.34',
            'instance_ip_family': 'v4',
            'instance_ip_mode': 'active-active',
            'secondary_ip_tracking_ip': SubnetType('10.100.1.0', '24'),
            'subnet_uuid': '9b5e5547-a7b2-4cd2-99ed-87eff7ed34da',
            'instance_ip_subscriber_tag': 'test',
            'instance_ip_secondary': False,
            'instance_ip_local_ip': False,
            'service_instance_ip': False,
            'service_health_check_ip': False,
            'instance_ip_subnet': SubnetType('10.100.0.0', '24'),
            'annotations': {},
            'display_name': 'some tekst'
        }
        obj = self.set_properties(InstanceIp(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_appliance_set_create(self):
        prop_map = {
            'name': 'service appliance test name',
            'parent_obj': self.gsc,
            'service_appliance_set_virtualization_type': 'virtual-machine',
            'service_appliance_set_properties': {},
            'service_appliance_driver': 'Juniper',
            'service_appliance_ha_mode': 'active-active',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceApplianceSet(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_route_target_create(self):
        prop_map = {
            'name': 'default',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoteTarget(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_listener_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'loadbalancer_listener_properties': LoadbalancerListenerType(
                protocol='HTTP', protocol_port=80, connection_limit=15),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerListener(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_floating_ip_pool_create(self):
        prop_map = {
            'floating_ip_pool_subnets': FloatingIpPoolSubnetType(
                subnet_uuid='9b5e5547-a7b2-4cd2-99ed-87eff7ed34da'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FloatingIpPool(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_config_root_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ConfigRoot(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_template_create(self):
        prop_map = {
            'service_template_properties': ServiceTemplateType(
                version=12, service_mode='transparent',
                service_type='firewall', service_scaling=False,
                interface_type=ServiceTemplateInterfaceType(
                    service_interface_type='left',
                    shared_ip=False, static_route_enable=True),
                ordered_interfaces=False, availability_zone_enable=True,
                service_virtualization_type='virtual-machine',
                vrouter_instance_type='libvirt-qemu'),
            'service_config_managed': False,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceTemplate(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_firewall_policy_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'draft_mode_state': 'created',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FirewallPolicy(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_route_table_create(self):
        prop_map = {
            'routes': RouteTableType(route=RouteType(
                prefix='10.100.77.1',
                next_hop='10.100.77.13',
                next_hop_type='ip-address')),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RouteTable(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_provider_attachment_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ProviderAttachment(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_overlay_role_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(OverlayRole(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_multicast_policy_create(self):
        prop_map = {
            'multicast_source_groups': MulticastSourceGroups(
                multicast_source_group=MulticastSourceGroup(
                    name='test name',
                    path='/etc/contrail/foo',
                    rate='')),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(MulticastPolicy(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_network_device_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NetworkDeviceConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_dns_record_create(self):
        prop_map = {
            'virtual_DNS_record_data': VirtualDnsRecordType(
                record_name='foo', record_type='CNAME',
                record_class='IN', record_data='foo'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualDnsRecord(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_dsa_rule_create(self):
        dsa = DiscoveryServiceAssignment(name=self.id())
        self.api.discovery_service_assignment_create(dsa)

        prop_map = {
            'parent_obj': dsa,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DsaRule(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_config_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_discovery_service_assignment_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DiscoveryServiceAssignment(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_logical_interface_create(self):
        pr = PhisicalRouter(name=self.id())
        self.api.physical_router_create(pr)

        prop_map = {
            'parent_obj': pr,
            'logical_interface_vlan_tag': 2,
            'logical_interface_type': 'l3',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LogicalInterface(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_flow_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'flow_node_ip_address': '10.100.0.222',
            'flow_node_load_balancer_ip': '10.100.2.12',
            'flow_node_inband_interface': 'some string',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FlowNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_group_create(self):
        node = Node(name=self.id())
        self.api.node_create(node)
        prop_map = {
            'parent_obj': node,
            'bms_port_group_info': BaremetalPortGroupInfo(
                standalone_ports_supported=False,
                node_uuid=node.uuid,
            ),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PortGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_route_aggregate_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'aggregate_route_entries': RouteListType(route='a string'),
            'aggregate_route_nexthop': 'some string',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RouteAggregate(), prop_map)
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

    def test_domain_create(self):
        cr = ConfigRoot(name=self.id())
        self.api.config_root_create(cr)

        prop_map = {
            'parent_obj': cr,
            'domain_limits': DomainLimitsType(
                project_limit=10, virtual_network_limit=100,
                security_group_limit=5),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Domain(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_hostname_record_create(self):
        prop_map = {
            'structured_syslog_hostaddr': '10.100.0.124',
            'structured_syslog_tenant': 'tenant name',
            'structured_syslog_location': '/var/log/foo/',
            'structured_syslog_device': '9b5e5547-a7b2-4cd2-99ed-87eff7ed34da',
            'structured_syslog_hostname_tags': 'some,tags',
            'structured_syslog_linkmap': StructuredSyslogLinkmap(
                links=StructuredSyslogLinkType(
                    overlay='', underlay='', link_type='',
                    traffic_destination='', metadata='')),
            'structured_syslog_lan_segment_list':
                StructuredSyslogLANSegmentList(
                    LANSegmentList=StructuredSyslogLANSegmentType(
                        vpn='', network_ranges='')),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogHostnameRecord(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_instance_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'service_instance_properties': ServiceInstanceType(
                auto_policy=True, availability_zone='ZoneOne',
                interface_list=ServiceInstanceInterfaceType(
                    virtual_network='test')),
            'service_instance_bindings': {},
            'service_instance_bgp_enabled': True,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceInstance(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_node_profile_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'node_profile_type': 'end-system',
            'node_profile_vendor': 'Juniper',
            'node_profile_device_family': 'device family name',
            'node_profile_hitless_upgrade': True,
            'node_profile_roles': NodeProfileRolesType(
                role_mappings=NodeProfileRoleType(
                    physical_role='a string', rb_roles='a string')),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NodeProfile(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bridge_domain_create(self):
        vn = VirtualNetwork(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.virtual_network_create(vn)

        prop_map = {
            'parent_obj': vn,
            'mac_learning_enabled': False,
            'mac_limit_control': MACLimitControlType(
                mac_limit=23, mac_limit_action='alarm'),
            'mac_move_control': MACMoveLimitControlType(
                mac_move_limit=10, mac_move_time_window=23,
                mac_move_limit_action='drop'),
            'mac_aging_time': 1423,
            'isid': 3435,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(BridgeDomain(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_alias_ip_create(self):
        vn = VirtualNetwork(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.virtual_network_create(vn)
        aip_pool = AliasIpPool(self.id(), parent_obj=vn)
        self.api.alias_ip_pool_create(aip_pool)

        prop_map = {
            'parent_obj': aip_pool,
            'alias_ip_address': '127.12.12.12',
            'alias_ip_address_family': 'v4',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AliasIp(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_webui_node_create(self):
        prop_map = {
            'webui_node_ip_address': '197.12.31.11',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(WebuiNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Port(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bgp_as_a_service_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(BgpAsAService(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_subnet_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Subnet(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_sub_cluster_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(SubCluster(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_forwarding_class_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ForwardingClass(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_group_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_global_analytics_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(GlobalAnalyticsConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_address_group_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AddressGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_application_policy_set_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ApplicationPolicySet(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_ip_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualIp(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_intent_map_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(IntentMap(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_tuple_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PortTuple(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_analytics_alarm_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AnalyticsAlarmNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_qos_queue_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(QosQueue(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_physical_role_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PhysicalRole(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_card_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Card(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_security_logging_object_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(SecurityLoggingObject(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_qos_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(QosConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_analytics_snmp_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AnalyticsSnmpNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_machine_interface_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualMachineInterface(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_cli_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(CliConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_object_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceObject(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_feature_flag_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FeatureFlag(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Loadbalancer(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_tenant_record_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogTenantRecord(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_peering_policy_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PeeringPolicy(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_application_record_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogApplicationRecord(),
                                  prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_global_vrouter_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(GlobalVrouterConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_floating_ip_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FloatingIp(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_link_aggregation_group_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LinkAggregationGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_router_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualRouter(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_profile_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PortProfile(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_policy_management_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PolicyManagement(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_e2_service_provider_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(E2ServiceProvider(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_routing_policy_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoutingPolicy(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_role_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoleConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_tag_type_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(TagType(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_message_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogMessage(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_pool_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerPool(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_global_qos_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(GlobalQosConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_analytics_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AnalyticsNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_dns_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualDns(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_config_database_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ConfigDatabaseNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_config_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ConfigNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_firewall_rule_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FirewallRule(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bgp_vpn_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Bgpvpn(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_role_definition_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoleDefinition(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_connection_module_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceConnectionModule(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_security_group_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(SecurityGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_database_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DatabaseNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_health_monitor_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerHealthmonitor(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_devicemgr_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DevicemgrNode(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_fabric_namespace_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FabricNamespace(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_network_ipam_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NetworkIpam(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_network_policy_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NetworkPolicy(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_hardware_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Hardware(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_tag_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Tag(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_feature_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FeatureConfig(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bgp_router_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(BgpRouter(), prop_map)
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

    def test_virtual_port_group_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualPortGroup(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_appliance_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceAppliance(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_namespace_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Namespace(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_feature_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Feature(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_device_image_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DeviceImage(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_physical_interface_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PhysicalInterface(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_access_control_list_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AccessControlList(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_node_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Node(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_customer_attachment_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(CustomerAttachment(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_sla_profile_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogSlaProfile(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_host_based_service_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(HostBasedService(), prop_map)
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

    def test_interface_route_table_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(InterfaceRouteTable(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_member_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerMember(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_health_check_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceHealthCheck(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_alarm_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Alarm(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_api_access_list_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ApiAccessList(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_alias_ip_pool_create(self):
        vn = VirtualNetwork(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.virtual_network_create(vn)

        prop_map = {
            'parent_obj': vn,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AliasIpPool(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_data_center_interconnect_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DataCenterInterconnect(), prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)
