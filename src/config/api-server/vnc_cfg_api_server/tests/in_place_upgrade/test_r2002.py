#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging
import unittest

import six
from vnc_api.exceptions import RefsExistError
# Pawel Z.: I decided to import with an asterisk because in this file
# I need all available vnc_api objects. Importing hundreds of objects
# line by line isn't effective and introduces a lot of unnecessary noise.
from vnc_api.vnc_api import *  # noqa: F403

from vnc_cfg_api_server.tests.in_place_upgrade import test_case

logger = logging.getLogger(__name__)


class TestInPlaceUpgradeR2002(test_case.InPlaceUpgradeTestCase):
    def setUp(self, **kwargs):
        super(TestInPlaceUpgradeR2002, self).setUp()
        gsc_fq_name = GlobalSystemConfig().fq_name
        self.gsc = self.api.global_system_config_read(gsc_fq_name)

    def id(self):  # noqa: A003
        """ID method is a workaround for conflicting names.

        While run tests under PY2 and PY3 at the same time.
        The other way would be to randomize fq_names for each object.
        :return:
        """
        py_v = '-py2' if six.PY2 else '-py3'
        return super(TestInPlaceUpgradeR2002, self).id() + py_v

    @property
    def api(self):
        return self._vnc_lib

    def test_enable_4byte_asn(self):
        self.gsc.enable_4byte_as = True
        self.api.global_system_config_update(self.gsc)

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

        obj = self.set_properties(JobTemplate, prop_map)
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

        obj = self.set_properties(Fabric, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_device_chassis_create(self):
        prop_map = {'name': 'dc-{}'.format(self.id()),
                    'annotations': {},
                    'device_chassis_type': 'fake_chassis_type',
                    'display_name': 'device chassis'}

        obj = self.set_properties(DeviceChassis, prop_map)
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

        obj = self.set_properties(DeviceFunctionalGroup, prop_map)
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

        obj = self.set_properties(HardwareInventory, prop_map)
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

        obj = self.set_properties(PhysicalRouter, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_telemetry_profile_create(self):
        project = self._project_fetch_or_create(self.id())
        prop_map = {'name': 'tp-{}'.format(self.id()),
                    'annotations': {},
                    'parent_obj': project,
                    'parent_type': 'project',
                    'display_name': 'some_string',
                    'telemetry_profile_is_default': False}

        obj = self.set_properties(TelemetryProfile, prop_map)
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

        obj = self.set_properties(StormControlProfile, prop_map)
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

        obj = self.set_properties(SflowProfile, prop_map)
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
                    'parent_type': vn.object_type,
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

        obj = self.set_properties(RoutingInstance, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_control_node_zone_create(self):
        prop_map = {
            'name': 'cnz-{}'.format(self.id()),
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ControlNodeZone, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_endpoint_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'service endpoint name'
        }
        obj = self.set_properties(ServiceEndpoint, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_instance_ip_create(self):
        net_ipam = NetworkIpam(name=self.id())
        net_ipam.uuid = self.api.network_ipam_create(net_ipam)

        vn = VirtualNetwork(name=self.id())
        vn_properties = VirtualNetworkType()
        vn_properties.set_vxlan_network_identifier(2001 if six.PY2 else 2002)
        vn_properties.set_forwarding_mode('l2_l3')
        vn.set_virtual_network_properties(vn_properties)
        vn.add_network_ipam(net_ipam, VnSubnetsType(
            [IpamSubnetType(SubnetType('10.100.0.0', 16))]))
        vn.uuid = self.api.virtual_network_create(vn)

        prop_map = {
            'name': self.id(),
            'instance_ip_address': '10.100.0.34',
            'instance_ip_family': 'v4',
            'instance_ip_mode': 'active-standby',
            'secondary_ip_tracking_ip': None,
            'subnet_uuid': None,
            'instance_ip_subscriber_tag': 'somestring',
            'instance_ip_secondary': False,
            'instance_ip_local_ip': False,
            'service_instance_ip': False,
            'service_health_check_ip': False,
            'instance_ip_subnet': None,
            'annotations': {},
            'display_name': 'some text'
        }
        obj = self.set_properties(InstanceIp, prop_map)
        obj.set_virtual_network(vn)
        obj.set_network_ipam(net_ipam)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_appliance_set_create(self):
        prop_map = {
            'name': 'service appliance test name',
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'service_appliance_set_virtualization_type': 'virtual-machine',
            'service_appliance_set_properties': {},
            'service_appliance_driver': 'Juniper',
            'service_appliance_ha_mode': 'active-active',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceApplianceSet, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_route_target_create(self):
        prop_map = {
            'name': 'target:1:1',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RouteTarget, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_listener_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'loadbalancer_listener_properties': LoadbalancerListenerType(
                protocol='HTTP', protocol_port=80, connection_limit=15),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerListener, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_config_root_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ConfigRoot, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_template_create(self):
        prop_map = {
            'service_template_properties': ServiceTemplateType(
                version=12, service_mode='transparent',
                service_type='firewall', service_scaling=False,
                interface_type=[ServiceTemplateInterfaceType(
                    service_interface_type='left',
                    shared_ip=False, static_route_enable=True)],
                ordered_interfaces=False, availability_zone_enable=True,
                service_virtualization_type='virtual-machine',
                vrouter_instance_type='libvirt-qemu'),
            'service_config_managed': False,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceTemplate, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_firewall_policy_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FirewallPolicy, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_route_table_create(self):
        prop_map = {
            'routes': RouteTableType(route=[RouteType(
                prefix='10.100.77.1',
                next_hop='10.100.77.13',
                next_hop_type='ip-address')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RouteTable, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_provider_attachment_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ProviderAttachment, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_overlay_role_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(OverlayRole, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_multicast_policy_create(self):
        prop_map = {
            'multicast_source_groups': MulticastSourceGroups(
                multicast_source_group=[MulticastSourceGroup(
                    name='test name', path='/etc/contrail/foo', rate='')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(MulticastPolicy, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_network_device_config_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NetworkDeviceConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_dns_record_create(self):
        domain = Domain(name='custom-{}'.format(self.id()))
        self.api.domain_create(domain)

        virtual_dns = VirtualDns('inplaceupgrade',
                                 parent_obj=domain,
                                 virtual_DNS_data=VirtualDnsType(
                                     domain_name='www',
                                     dynamic_records_from_client=True,
                                     record_order='random',
                                     default_ttl_seconds=23))
        self.api.virtual_DNS_create(virtual_dns)

        prop_map = {
            'parent_obj': virtual_dns,
            'parent_type': virtual_dns.object_type,
            'virtual_DNS_record_data': VirtualDnsRecordType(
                record_name='www', record_type='CNAME',
                record_class='IN', record_data='foo',
                record_ttl_seconds=123),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualDnsRecord, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_dsa_rule_create(self):
        dsa = DiscoveryServiceAssignment(name=self.id())
        self.api.discovery_service_assignment_create(dsa)

        prop_map = {
            'parent_obj': dsa,
            'parent_type': dsa.object_type,
            'dsa_rule_entry': DiscoveryServiceAssignmentType(
                publisher=DiscoveryPubSubEndPointType(
                    ep_type='some string'),
                subscriber=[DiscoveryPubSubEndPointType(
                    ep_type='some string')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DsaRule, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_config_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_discovery_service_assignment_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DiscoveryServiceAssignment, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_logical_interface_create(self):
        pr = PhysicalRouter(name=self.id())
        self.api.physical_router_create(pr)

        prop_map = {
            'parent_obj': pr,
            'parent_type': pr.object_type,
            'logical_interface_vlan_tag': 2,
            'logical_interface_type': 'l3',
            'logical_interface_port_params': PortParameters(
                port_disable=False,
                port_mtu=1500,
                port_description='some string'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LogicalInterface, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_flow_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'flow_node_ip_address': '10.100.0.222',
            'flow_node_load_balancer_ip': '10.100.2.12',
            'flow_node_inband_interface': 'some string',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FlowNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_group_create(self):
        node = Node(name=self.id())
        self.api.node_create(node)
        prop_map = {
            'parent_obj': node,
            'parent_type': node.object_type,
            'bms_port_group_info': BaremetalPortGroupInfo(
                standalone_ports_supported=False,
                node_uuid=node.uuid,
            ),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PortGroup, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_route_aggregate_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'aggregate_route_entries': RouteListType(route=['100.0.0.0/24']),
            'aggregate_route_nexthop': 'some string',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RouteAggregate, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_logical_router_create(self):
        project = self._project_fetch_or_create(self.id())
        lr_name = 'lr-{}'.format(self.id())
        ip = IpAddressesType(["1.1.1.1"])
        prop_map = {
            "name": lr_name,
            'parent_type': project.object_type,
            "parent_obj": project,
            "display_name": lr_name,
            "fq_name": ['lr' + lr_name],
            "id_perms": IdPermsType(enable=True),
            "logical_router_gateway_external": False,
            "logical_router_type": 'vxlan-routing',
            "vxlan_network_identifier": '1111' if six.PY2 else '1212',
            "configured_route_target_list": None,
            "logical_router_dhcp_relay_server": ip,
            'annotations': {}
        }

        obj = self.set_properties(LogicalRouter, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_hostname_record_create(self):
        ssc = StructuredSyslogConfig(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.structured_syslog_config_create(ssc)

        prop_map = {
            'parent_obj': ssc,
            'parent_type': ssc.object_type,
            'structured_syslog_hostaddr': '10.100.0.124',
            'structured_syslog_tenant': 'tenant name',
            'structured_syslog_location': '/var/log/foo/',
            'structured_syslog_device': '9b5e5547-a7b2-4cd2-99ed-87eff7ed34da',
            'structured_syslog_hostname_tags': 'some,tags',
            'structured_syslog_linkmap': StructuredSyslogLinkmap(
                links=[StructuredSyslogLinkType(
                    overlay='', underlay='', link_type='',
                    traffic_destination='', metadata='')]),
            'structured_syslog_lan_segment_list':
                StructuredSyslogLANSegmentList(
                    LANSegmentList=[StructuredSyslogLANSegmentType(
                        vpn='', network_ranges='')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogHostnameRecord, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_instance_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'service_instance_properties': ServiceInstanceType(
                auto_policy=True, availability_zone='ZoneOne',
                interface_list=[ServiceInstanceInterfaceType(
                    virtual_network='test')]),
            'service_instance_bindings': {},
            'service_instance_bgp_enabled': True,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceInstance, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_node_profile_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'node_profile_type': 'end-system',
            'node_profile_vendor': 'Juniper',
            'node_profile_device_family': 'device family name',
            'node_profile_hitless_upgrade': True,
            'node_profile_roles': NodeProfileRolesType(
                role_mappings=[NodeProfileRoleType(
                    physical_role='admin', rb_roles=['admin'])]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NodeProfile, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bridge_domain_create(self):
        vn = VirtualNetwork(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.virtual_network_create(vn)

        prop_map = {
            'parent_obj': vn,
            'parent_type': vn.object_type,
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
        obj = self.set_properties(BridgeDomain, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_webui_node_create(self):
        prop_map = {
            'webui_node_ip_address': '197.12.31.11',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(WebuiNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_create(self):
        node = Node(self.id())
        self.api.node_create(node)

        prop_map = {
            'parent_obj': node,
            'parent_type': node.object_type,
            'port_group_uuid': '74b6a80d-5c53-41c1-8ca1-88ff8d044a11',
            'bms_port_info': BaremetalPortInfo(
                pxe_enabled=True, local_link_connection=LocalLinkConnection(
                    switch_info='48bf14a6-b780-49fd-beb1-597b7361ebb2',
                    port_index='12', port_id='123', switch_id='5'),
                node_uuid=node.uuid, address='00-10-FA-6E-38-4A'),
            'esxi_port_info': ESXIProperties(dvs_name='some string',
                                             dvs_id='M0id'),
            'label': 'some string',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Port, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bgp_as_a_service_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'autonomous_system': self.gsc.autonomous_system,
            'bgpaas_shared': False,
            'bgpaas_ip_address': '172.142.142.1',
            'bgpaas_session_attributes': BgpSessionAttributes(
                bgp_router=None, admin_down=True, passive=False,
                as_override=False, hold_time=15503, loop_count=4,
                local_autonomous_system=124,
                address_families=AddressFamilies(family=['inet']),
                auth_data=AuthenticationData(
                    key_type='md5', key_items=[AuthenticationKeyItem(
                        key_id=0, key='somestring')]),
                family_attributes=[BgpFamilyAttributes(
                    address_family='inet', loop_count=4,
                    prefix_limit=BgpPrefixLimit(
                        maximum=16, idle_timeout=40000),
                    default_tunnel_encap=['vxlan'])],
                private_as_action='remove',
                route_origin_override=RouteOriginOverride()),
            'bgpaas_ipv4_mapped_ipv6_nexthop': False,
            'bgpaas_suppress_route_advertisement': True,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(BgpAsAService, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_subnet_create(self):
        prop_map = {
            'subnet_ip_prefix': SubnetType(ip_prefix='192.168.0.0',
                                           ip_prefix_len=16),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Subnet, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_sub_cluster_create(self):
        prop_map = {
            'name': 'test-subcluster-{}'.format(self.id()),
            'annotations': {},
            'display_name': 'some string',
            'sub_cluster_asn': 124,
        }
        obj = self.set_properties(SubCluster, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_forwarding_class_create(self):
        prop_map = {
            'forwarding_class_dscp': 0,
            'forwarding_class_mpls_exp': 2,
            'forwarding_class_vlan_priority': 1,
            'forwarding_class_id': 0,
            'sub_cluster_asn': 12,
            'sub_cluster_id': 235,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ForwardingClass, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_group_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'service_group_firewall_service_list': FirewallServiceGroupType(
                firewall_service=[FirewallServiceType(
                    protocol='udp', protocol_id=17,
                    src_ports=PortType(start_port=1, end_port=5),
                    dst_ports=PortType(start_port=6, end_port=10),
                )]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceGroup, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_global_analytics_config_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(GlobalAnalyticsConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_address_group_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'address_group_prefix': SubnetListType(subnet=[SubnetType(
                ip_prefix='192.168.0.0', ip_prefix_len=16)]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AddressGroup, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_application_policy_set_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ApplicationPolicySet, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_ip_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'virtual_ip_properties': VirtualIpType(
                address='192.168.1.1', status='UP',
                status_description='some string', admin_state=True,
                protocol='HTTP', protocol_port=80, connection_limit=5,
                subnet_id='b48448f8-33ee-4f79-a530-e400c5e8d930',
                persistence_cookie_name='somestring',
                persistence_type='APP_COOKIE'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualIp, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_intent_map_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'intent_map_intent_type': 'assisted-replicator',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(IntentMap, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_port_tuple_create(self):
        si = ServiceInstance(
            name=self.id(), service_instance_properties=ServiceInstanceType(
                auto_policy=True),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.service_instance_create(si)
        prop_map = {
            'parent_obj': si,
            'parent_type': si.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PortTuple, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_analytics_alarm_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'analytics_alarm_node_ip_address': '172.172.10.10',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AnalyticsAlarmNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_qos_queue_create(self):
        qos_cfg = GlobalQosConfig(name=self.id(), parent_obj=self.gsc,
                                  control_traffic_dscp=ControlTrafficDscpType(
                                      control=10, analytics=5, dns=2))
        self.api.global_qos_config_create(qos_cfg)

        prop_map = {
            'parent_obj': qos_cfg,
            'parent_type': qos_cfg.object_type,
            'min_bandwidth': 1255,
            'max_bandwidth': 4634,
            'qos_queue_identifier': 35,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(QosQueue, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_physical_role_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PhysicalRole, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_card_create(self):
        prop_map = {
            'interface_map': InterfaceMapType(port_info=[PortInfoType(
                name='some string', type='fc', port_speed='10G',
                channelized=True, channelized_port_speed='1G',
                port_group='some string', labels=['some', 'strings'])]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Card, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_security_logging_object_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'security_logging_object_rules': SecurityLoggingObjectRuleListType(
                rule=[SecurityLoggingObjectRuleEntryType(
                    rule_uuid='f1abda8b-5456-4774-a741-b4d236a7ba8e',
                    rate=23)]),
            'security_logging_object_rate': 23,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(SecurityLoggingObject, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_qos_config_create(self):
        qos_forwarding = QosIdForwardingClassPairs(
            qos_id_forwarding_class_pair=[
                QosIdForwardingClassPair(key=1, forwarding_class_id=255),
            ])

        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'qos_config_type': 'vhost',
            'dscp_entries': qos_forwarding,
            'vlan_priority_entries': qos_forwarding,
            'mpls_exp_entries': qos_forwarding,
            'default_forwarding_class_id': 125,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(QosConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_analytics_snmp_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'analytics_snmp_node_ip_address': '192.168.10.10',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AnalyticsSnmpNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_machine_interface_create(self):
        vn = VirtualNetwork(self.id())
        self.api.virtual_network_create(vn)

        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'ecmp_hashing_include_fields': EcmpHashingIncludeFields(
                hashing_configured=True, source_ip=True,
                destination_ip=True, ip_protocol=True,
                source_port=False, destination_port=False),
            'port_security_enabled': True,
            'virtual_machine_interface_mac_addresses': MacAddressesType(),
            'virtual_machine_interface_dhcp_option_list':
                DhcpOptionsListType(dhcp_option=[DhcpOptionType(
                    dhcp_option_name='an option name',
                    dhcp_option_value='an option value',
                    dhcp_option_value_bytes='some string')]),
            'virtual_machine_interface_host_routes': RouteTableType(
                route=[RouteType(prefix='10.10.100.0/24',
                                 next_hop='10.10.101.20',
                                 next_hop_type='ip-address')]),
            'virtual_machine_interface_allowed_address_pairs':
                AllowedAddressPairs(allowed_address_pair=[AllowedAddressPair(
                    ip=SubnetType(ip_prefix='10.10.100.0', ip_prefix_len=24),
                    mac='8:0:27:90:7a:75', address_mode='active-active')]),
            'vrf_assign_table': VrfAssignTableType(
                vrf_assign_rule=[VrfAssignRuleType(
                    match_condition=MatchConditionType(
                        protocol='UDP', ethertype='IPv4'),
                    vlan_tag=23, routing_instance='somestring',
                    ignore_acl=False)]),
            'virtual_machine_interface_device_owner': 'some string',
            'virtual_machine_interface_disable_policy': False,
            'virtual_machine_interface_properties':
                VirtualMachineInterfacePropertiesType(
                    service_interface_type='left',
                    interface_mirror=InterfaceMirrorType(
                        traffic_direction='ingress',
                        mirror_to=MirrorActionType(
                            analyzer_ip_address='10.10.100.24',
                            routing_instance='some string')),
                    local_preference=10, sub_interface_vlan_tag=23,
                    max_flows=235325),
            'virtual_machine_interface_bindings': {'key_value_pair': tuple()},
            'virtual_machine_interface_fat_flow_protocols': FatFlowProtocols(
                fat_flow_protocol=[ProtocolType(
                    protocol='UDP', port=22, ignore_address='none')]),
            'vlan_tag_based_bridge_domain': False,
            'igmp_enable': True,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualMachineInterface, prop_map)
        obj.set_virtual_network(vn)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_cli_config_create(self):
        pr = PhysicalRouter(name=self.id())
        self.api.physical_router_create(pr)

        prop_map = {
            'parent_obj': pr,
            'parent_type': pr.object_type,
            'accepted_cli_config': 'some string',
            'commit_diff_list': CliDiffListType(
                commit_diff_info=[CliDiffInfoType(
                    username='admin', time='2020-06-06 22:00:32',
                    config_changes='some string')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(CliConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_object_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceObject, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_feature_flag_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'feature_id': 'default',
            'feature_flag_version': 'R2008',
            'enable_feature': True,
            'feature_state': 'experimental',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FeatureFlag, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'loadbalancer_provider': 'Juniper',
            'loadbalancer_properties': LoadbalancerType(),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Loadbalancer, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_tenant_record_create(self):
        ssc = StructuredSyslogConfig(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.structured_syslog_config_create(ssc)

        prop_map = {
            'parent_obj': ssc,
            'parent_type': ssc.object_type,
            'structured_syslog_tenant': 'admin',
            'structured_syslog_tenantaddr': 'some string',
            'structured_syslog_tenant_tags': 'some,tags',
            'structured_syslog_dscpmap': StructuredSyslogDSCPMap(
                dscpListIPv4=[StructuredSyslogDSCPType(
                    dscp_value='some string', alias_code='some string')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogTenantRecord, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_peering_policy_create(self):
        prop_map = {
            'peering_service': 'public-peering',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PeeringPolicy, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_application_record_create(self):
        ssc = StructuredSyslogConfig(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.structured_syslog_config_create(ssc)

        prop_map = {
            'parent_obj': ssc,
            'parent_type': ssc.object_type,
            'structured_syslog_app_category': 'some string',
            'structured_syslog_app_subcategory': 'yet another string',
            'structured_syslog_app_groups': 'a string',
            'structured_syslog_app_risk': 'string',
            'structured_syslog_app_service_tags': 'red,blue,green',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogApplicationRecord,
                                  prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_floating_ip_create(self):
        net_ipam = NetworkIpam(
            name=self.id(),
            ipam_subnet_method='flat-subnet',
            ipam_subnets=IpamSubnets(subnets=[
                IpamSubnetType(subnet=SubnetType(ip_prefix='10.100.0.0',
                                                 ip_prefix_len=16))]))
        self.api.network_ipam_create(net_ipam)

        vn = VirtualNetwork(
            name=self.id(),
            virtual_network_properties=VirtualNetworkType(
                forwarding_mode='l3'))
        vn.add_network_ipam(net_ipam, VnSubnetsType())
        self.api.virtual_network_create(vn)

        fip_pool = FloatingIpPool(
            parent_obj=vn, name=self.id())
        self.api.floating_ip_pool_create(fip_pool)

        prop_map = {
            'parent_obj': fip_pool,
            'parent_type': fip_pool.object_type,
            'floating_ip_address': '10.100.0.123',
            'floating_ip_is_virtual_ip': False,
            'floating_ip_fixed_ip_address': '10.10.100.10',
            'floating_ip_address_family': 'v4',
            'floating_ip_port_mappings_enable': False,
            'floating_ip_port_mappings': PortMappings(port_mappings=[
                PortMap(protocol='TCP', src_port=9124, dst_port=9354)]),
            'floating_ip_traffic_direction': 'ingress',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FloatingIp, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_link_aggregation_group_create(self):
        pr = PhysicalRouter(name=self.id())
        self.api.physical_router_create(pr)

        prop_map = {
            'parent_obj': pr,
            'parent_type': pr.object_type,
            'link_aggregation_group_lacp_enabled': True,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LinkAggregationGroup, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_router_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'virtual_router_type': 'embedded',
            'virtual_router_dpdk_enabled': True,
            'virtual_router_ip_address': '10.100.124.12',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualRouter, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'port_profile_params': PortProfileParameters(
                port_params=PortParameters(port_disable=False, port_mtu=1500,
                                           port_description='some string'),
                lacp_params=LacpParams(lacp_enable=True, lacp_interval='slow',
                                       lacp_mode='passive'),
                flow_control=False,
                bpdu_loop_protection=False,
                port_cos_untrust=False),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PortProfile, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_policy_management_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PolicyManagement, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_e2_service_provider_create(self):
        prop_map = {
            'e2_service_provider_promiscuous': True,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(E2ServiceProvider, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_routing_policy_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'routing_policy_entries': PolicyStatementType(
                term=[PolicyTermType(
                    term_match_condition=TermMatchConditionType(
                        protocol=['xmpp'], prefix=[PrefixMatchType(
                            prefix='some string', prefix_type='exact')],
                        community='some string',
                        community_list=['some string'],
                        community_match_all=True,
                        extcommunity_list=['some string'],
                        extcommunity_match_all=False, family='inet',
                        as_path=[124], external='ospf-type-1', local_pref=124,
                        nlri_route_type=[124], prefix_list=[
                            PrefixListMatchType(
                                interface_route_table_uuid=[
                                    '0f107c6b-0d36-4f3e-b6e4-7a2c5372617e'],
                                prefix_type='longer')],
                        route_filter=RouteFilterType(
                            route_filter_properties=[RouteFilterProperties(
                                route='some string', rote_type='longer',
                                route_type_value='some string')]),
                        term_action_list=TermActionListType(
                            update=ActionUpdateType(
                                as_path=ActionAsPathType(
                                    expand=AsListType(asn_list=235)),
                                community=ActionCommunityType(
                                    add=CommunityListType(
                                        community='no-export'),
                                    remove=CommunityListType(
                                        community='no-export'),
                                    set=CommunityListType(
                                        community='no-export')),
                                extcommunity=ActionExtCommunityType(
                                    add=CommunityListType(
                                        community='no-export'),
                                    remove=CommunityListType(
                                        community='no-export'),
                                    set=CommunityListType(
                                        community='no-export')),
                                local_pref=124, med=125), action='reject',
                            external='ospf-type-1',
                            as_path_expand='some string',
                            as_path_prepend='some string')))]),
            'term_type': 'vrouter',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoutingPolicy, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_role_config_create(self):
        node_profile = NodeProfile(name=self.id(), parent_obj=self.gsc)
        self.api.node_profile_create(node_profile)

        prop_map = {
            'parent_obj': node_profile,
            'parent_type': node_profile.object_type,
            'role_config_config': 'some string',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoleConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_message_create(self):
        ssc = StructuredSyslogConfig(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.structured_syslog_config_create(ssc)

        field_names_list = FieldNamesList(field_names=['some_field_name'])

        prop_map = {
            'parent_obj': ssc,
            'parent_type': ssc.object_type,
            'structured_syslog_message_tagged_fields': field_names_list,
            'structured_syslog_message_integer_fields': field_names_list,
            'structured_syslog_message_process_and_store': False,
            'structured_syslog_message_process_and_summarize': True,
            'structured_syslog_message_process_and_summarize_user': False,
            'structured_syslog_message_forward': 'do-not-forward',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(StructuredSyslogMessage, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_pool_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'loadbalancer_pool_properties': LoadbalancerPoolType(
                status='UP', status_description='ok', admin_state=True,
                protocol='HTTP', loadbalancer_method='ROUND_ROBIN',
                subnet_id='432c6811-dba6-411e-9152-d8a40a9e38b3',
                session_persistence='APP_COOKIE',
                persistence_cookie_name='some string'),
            'loadbalancer_pool_provider': 'silver',
            'loadbalancer_pool_custom_attributes': {},
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerPool, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_global_qos_config_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'control_traffic_dscp': ControlTrafficDscpType(control=10,
                                                           analytics=5,
                                                           dns=2),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(GlobalQosConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_analytics_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'analytics_node_ip_address': '172.102.135.43',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AnalyticsNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_dns_create(self):
        domain = Domain(name='admin-{}'.format(self.id()))
        self.api.domain_create(domain)

        prop_map = {
            'parent_obj': domain,
            'parent_type': domain.object_type,
            'virtual_DNS_data': VirtualDnsType(
                domain_name='www', dynamic_records_from_client=True,
                record_order='random', default_ttl_seconds=23),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualDns, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_config_database_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'config_database_node_ip_address': '234.234.234.234',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ConfigDatabaseNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_config_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'config_node_ip_address': '111.10.110.24',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ConfigNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_firewall_rule_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'action_list': ActionListType(simple_action='deny'),
            'service': FirewallServiceType(
                protocol='udp', protocol_id=17,
                src_ports=PortType(start_port=1, end_port=5),
                dst_ports=PortType(start_port=6, end_port=10)),
            'endpoint_1': FirewallRuleEndpointType(any=True),
            'endpoint_2': FirewallRuleEndpointType(any=True),
            'match_tags': FirewallRuleMatchTagsType(tag_list=[
                'application', 'tier', 'site']),
            'match_tag_types': FirewallRuleMatchTagsTypeIdList(),
            'direction': '<>',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FirewallRule, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_bgp_vpn_create(self):
        rt_list = RouteTargetList(route_target=['target:3:1'])

        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'route_target_list': rt_list,
            'import_route_target_list': rt_list,
            'export_route_target_list': rt_list,
            'bgpvpn_type': 'l3',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Bgpvpn, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_role_definition_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(RoleDefinition, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_connection_module_create(self):
        prop_map = {
            'e2_service': 'point-to-point',
            'service_type': 'vpws-l2ckt',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceConnectionModule, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_database_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'database_node_ip_address': '234.234.234.234',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DatabaseNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_health_monitor_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'loadbalancer_healthmonitor_properties':
                LoadbalancerHealthmonitorType(
                    admin_state=True, monitor_type='PING', delay=10,
                    timeout=2424, max_retries=10, http_method='GET',
                    url_path='http://localhost/check', expected_codes='200'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerHealthmonitor, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_devicemgr_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'devicemgr_node_ip_address': '10.100.100.100',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DevicemgrNode, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_fabric_namespace_create(self):
        fabric = Fabric(name=self.id(), parent_obj=self.gsc)
        self.api.fabric_create(fabric)

        prop_map = {
            'parent_obj': fabric,
            'parent_type': fabric.object_type,
            'fabric_namespace_type': 'ASN',
            'fabric_namespace_value': NamespaceValue(
                ipv4_cidr=SubnetListType(subnet=[SubnetType(
                    ip_prefix='10.100.0.0', ip_prefix_len=16)]),
                asn=AutonomousSystemsType(asn=[self.gsc.autonomous_system])),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FabricNamespace, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_network_ipam_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'ipam_subnets': IpamSubnets(subnets=[IpamSubnetType(
                subnet=SubnetType(ip_prefix='10.100.0.0', ip_prefix_len=16))]),
            'ipam_subnet_method': 'flat-subnet',
            'ipam_subnetting': True,
            'network_ipam_mgmt': IpamType(
                ipam_method='dhcp', ipam_dns_method='none'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NetworkIpam, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_network_policy_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'network_policy_entries': PolicyEntriesType(
                policy_rule=[PolicyRuleType(
                    direction='<>', protocol='tcp', src_addresses=[AddressType(
                        virtual_network='124.23.23.12')],
                    src_ports=[PortType(start_port=10, end_port=12)],
                    dst_addresses=[AddressType(
                        virtual_network='125.23.23.12')],
                    dst_ports=[PortType(start_port=13, end_port=15)],
                    action_list=ActionListType(simple_action='deny'),
                    ethertype='IPv4')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(NetworkPolicy, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_hardware_create(self):
        prop_map = {
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Hardware, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_tag_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'tag_type_name': 'foo',
            'tag_value': 'bar',
            'tag_predefined': False,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Tag, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_feature_config_create(self):
        rd = RoleDefinition(name=self.id(), parent_obj=self.gsc)
        self.api.role_definition_create(rd)

        prop_map = {
            'parent_obj': rd,
            'parent_type': rd.object_type,
            'feature_config_additional_params': {},
            'feature_config_vendor_config': {},
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(FeatureConfig, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    @unittest.skip("Failing CAT test")
    def test_bgp_router_create(self):
        project = self._project_fetch_or_create(self.id())
        vn = VirtualNetwork(name='vn-{}'.format(self.id()), parent_obj=project)
        self.api.virtual_network_create(vn)
        ri = RoutingInstance(name=self.id(), parent_obj=vn)
        self.api.routing_instance_create(ri)

        prop_map = {
            'parent_obj': ri,
            'parent_type': ri.object_type,
            'bgp_router_parameters': BgpRouterParams(
                autonomous_system=self.gsc.autonomous_system,
                identifier='some string', address='10.10.10.10'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(BgpRouter, prop_map)
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
                    "mac_ip_learning_enable": True,
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

        obj = self.set_properties(VirtualNetwork, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_virtual_port_group_create(self):
        # port = Port(name=self.id())
        # port.uuid = self.api.port_create(port)

        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'virtual_port_group_lacp_enabled': True,
            'virtual_port_group_trunk_port_id': None,
            'virtual_port_group_user_created': True,
            'virtual_port_group_type': 'access',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(VirtualPortGroup, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_appliance_create(self):
        sas = ServiceApplianceSet(name=self.id(), parent_obj=self.gsc)
        self.api.service_appliance_set_create(sas)

        prop_map = {
            'parent_obj': sas,
            'parent_type': sas.object_type,
            'service_appliance_user_credentials': UserCredentials(),
            'service_appliance_ip_address': '10.100.10.100',
            'service_appliance_virtualization_type': 'virtual-machine',
            'service_appliance_properties': {},
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceAppliance, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_namespace_create(self):
        domain = Domain(name=self.id())
        self.api.domain_create(domain)

        prop_map = {
            'parent_obj': domain,
            'parent_type': domain.object_type,
            'namespace_cidr': SubnetType(ip_prefix='10.100.100.0',
                                         ip_prefix_len=24),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Namespace, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_feature_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Feature, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_device_image_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'device_image_file_name': 'some_file_name',
            'device_image_vendor_name': 'Juniper',
            'device_image_device_family': 'JunOS',
            'device_image_supported_platforms': DevicePlatformListType(),
            'device_image_os_version': '1',
            'device_image_file_uri': 'some string uri',
            'device_image_size': 235235,
            'device_image_md5': 'eb2a9193443f8aac2e9d83362f02fd86',
            'device_image_sha1': '3DA541559918A808C2402BBA5012F6C60B27661C',
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DeviceImage, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_physical_interface_create(self):
        pr = PhysicalRouter(name=self.id())
        self.api.physical_router_create(pr)

        prop_map = {
            'parent_obj': pr,
            'parent_type': pr.object_type,
            'ethernet_segment_identifier': '00:11:22:33:44:55:66:77:88:99',
            'physical_interface_type': 'regular',
            'physical_interface_mac_addresses': MacAddressesType(),
            'physical_interface_port_id': 'some string with ID',
            'physical_interface_lacp_force_up': False,
            'physical_interface_flow_control': False,
            'physical_interface_port_params': PortParameters(
                port_disable=False, port_mtu=1500,
                port_description='some string'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(PhysicalInterface, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_access_control_list_create(self):
        vn = VirtualNetwork(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.virtual_network_create(vn)

        prop_map = {
            'parent_obj': vn,
            'parent_type': vn.object_type,
            'access_control_list_entries': AclEntriesType(
                dynamic=True, acl_rule=[AclRuleType(
                    match_condition=MatchConditionType(protocol='UDP',
                                                       ethertype='IPv4'),
                    action_list=ActionListType(simple_action='deny'),
                )]),
            'access_control_list_hash': 23534214,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AccessControlList, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_node_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'node_type': 'baremetal',
            'esxi_info': ESXIHostInfo(
                username='admin', datacenter='default', esxi_name='testhost',
                cluster='test', mac='42:7f:33:d1:76:82', datastore='default',
                password='secret123', vcenter_server='default'),
            'ip_address': '10.10.10.10',
            'hostname': 'test-host',
            'bms_info': BaremetalServerInfo(
                network_interface='some string', driver='some string',
                properties=BaremetalProperties(), driver_info=DriverInfo(),
                name='some string'),
            'mac_address': '2e:37:05:05:54:b5',
            'disk_partition': 'sda,sdb',
            'interface_name': 'some string',
            'cloud_info': CloudInstanceInfo(
                os_version='1805', operating_system='centos7'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Node, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_customer_attachment_create(self):
        prop_map = {
            'attachment_address': AttachmentAddressType(),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(CustomerAttachment, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_structured_syslog_sla_profile_create(self):
        ssc = StructuredSyslogConfig(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.structured_syslog_config_create(ssc)

        prop_map = {
            'parent_obj': ssc,
            'parent_type': ssc.object_type,
            'annotations': {},
            'display_name': 'some string',
            'structured_syslog_sla_params': '',
        }
        obj = self.set_properties(StructuredSyslogSlaProfile, prop_map)
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

        obj = self.set_properties(VirtualMachine, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_interface_route_table_create(self):
        prop_map = {
            'interface_route_table_routes': RouteTableType(
                route=[RouteType(prefix='10.10.100.0/24',
                                 next_hop='10.10.101.20',
                                 next_hop_type='ip-address')]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(InterfaceRouteTable, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_load_balancer_member_create(self):
        lbp = LoadbalancerPool(
            name=self.id(),
            arent_obj=self._project_fetch_or_create(self.id()))
        self.api.loadbalancer_pool_create(lbp)

        prop_map = {
            'parent_obj': lbp,
            'parent_type': lbp.object_type,
            'loadbalancer_member_properties': LoadbalancerMemberType(
                admin_state=True, status='UP'),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(LoadbalancerMember, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_service_health_check_create(self):
        prop_map = {
            'parent_obj': self._project_fetch_or_create(self.id()),
            'parent_type': 'project',
            'service_health_check_properties': ServiceHealthCheckType(
                enabled=True, health_check_type='link-local',
                monitor_type='PING', delay=10, delayUsecs=1000, timeout=10,
                timeoutUsecs=1000, max_retries=2),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ServiceHealthCheck, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_alarm_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'uve_keys': UveKeysType(uve_key=['somestring']),
            'alarm_rules': AlarmOrList(or_list=[AlarmAndList(
                and_list=[AlarmExpression(
                    operation='==',
                    operand1='NodeStatus.process_info.process_state',
                    operand2=AlarmOperand2(
                        uve_attribute='NodeStatus.process_info.process_state',
                    ))])]),
            'alarm_severity': 0,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(Alarm, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_api_access_list_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'api_access_list_entries': RbacRuleEntriesType(
                rbac_rule=[RbacRuleType(
                    rule_object='config', rule_perms=[RbacPermType(
                        role_name='admin', role_crud='CRUD')])]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(ApiAccessList, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_alias_ip_pool_create(self):
        vn = VirtualNetwork(
            name=self.id(),
            parent_obj=self._project_fetch_or_create(self.id()))
        self.api.virtual_network_create(vn)

        prop_map = {
            'parent_obj': vn,
            'parent_type': vn.object_type,
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(AliasIpPool, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_data_center_interconnect_create(self):
        prop_map = {
            'parent_obj': self.gsc,
            'parent_type': self.gsc.object_type,
            'data_center_interconnect_bgp_hold_time': 10,
            'data_center_interconnect_mode': 'l3',
            'data_center_interconnect_bgp_address_families': AddressFamilies(
                family=['inet']),
            'data_center_interconnect_configured_route_target_list':
                RouteTargetList(route_target=['target:3:1']),
            'data_center_interconnect_type': 'inter_fabric',
            'destination_physical_router_list': LogicalRouterPRListType(
                logical_router_list=[LogicalRouterPRListParams(
                    logical_router_uuid='432c6811-dba6-411e-9152-d8a40a9e38b3',
                    physical_router_uuid_list=[
                        'f42862ae-45c1-4d70-b152-ee30d9caf985'])]),
            'annotations': {},
            'display_name': 'some string',
        }
        obj = self.set_properties(DataCenterInterconnect, prop_map)
        self.assertSchemaObjCreateOrUpdate(obj)

    def test_hbs_create(self):
        project = self._project_fetch_or_create(self.id())
        project.set_quota(QuotaType(host_based_service=1))
        self.api.project_update(project)
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=project)
        self.api.host_based_service_create(hbs)
