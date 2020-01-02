#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging
from unittest import skip

from vnc_api.vnc_api import AutonomousSystemsType
from vnc_api.vnc_api import DefaultProtocolType
from vnc_api.vnc_api import DeviceChassis
from vnc_api.vnc_api import DeviceFunctionalGroup
from vnc_api.vnc_api import DnsmasqLeaseParameters
from vnc_api.vnc_api import EnabledInterfaceParams
from vnc_api.vnc_api import ExecutableInfoListType, ExecutableInfoType
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import HardwareInventory
from vnc_api.vnc_api import JobTemplate
from vnc_api.vnc_api import JunosServicePorts
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import PlaybookInfoListType, PlaybookInfoType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import ProtocolBgpType, ProtocolOspfType
from vnc_api.vnc_api import RoutingBridgingRolesType
from vnc_api.vnc_api import RoutingInstance
from vnc_api.vnc_api import SNMPCredentials
from vnc_api.vnc_api import ServiceChainInfo
from vnc_api.vnc_api import SflowParameters, SflowProfile
from vnc_api.vnc_api import StaticRouteEntriesType, StaticRouteType
from vnc_api.vnc_api import StatsCollectionFrequency
from vnc_api.vnc_api import StormControlParameters, StormControlProfile
from vnc_api.vnc_api import TelemetryProfile
from vnc_api.vnc_api import TelemetryResourceInfo
from vnc_api.vnc_api import TelemetryStateInfo
from vnc_api.vnc_api import UserCredentials
from vnc_api.vnc_api import VirtualNetwork
from vnc_cfg_api_server.tests.in_place_upgrade import test_case

logger = logging.getLogger(__name__)


class TestInPlaceUpgradeR2002(test_case.InPlaceUpgradeTestCase):
    def setUp(self):
        super(TestInPlaceUpgradeR2002, self).setUp()
        gsc_fq_name = GlobalSystemConfig().fq_name
        self.gsc = self.api.global_system_config_read(gsc_fq_name)

    @property
    def api(self):
        return self._vnc_lib

    @skip
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
        prop_map = {
            'name': 'jt-%s' % self.id(),
            'display_name': 'job template test',
            'parent_obj': self.gsc,
            'job_template_executables': exe_info_list,
            'job_template_output_schema': None,
            'job_template_output_ui_schema': None,
            'job_template_concurrency_level': 'fabric',
            'job_template_description': 'test template',
            'job_template_type': 'workflow',
            'job_template_input_ui_schema': None,
            'job_template_synchronous_job': False,
            'job_template_input_schema': None,
            'job_template_playbooks': playbook_info_list,
            'annotations': None,
        }

        obj = self.set_properties(JobTemplate(), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_fabric_create(self):
        prop_map = {
            'name': 'f-%s' % self.id(),
            'display_name': 'fabric test',
            'fabric_ztp': True,
            'fabric_os_version': 'junos',
            'fabric_credentials': None,
            'fabric_enterprise_style': False,
            'annotations': None,
        }

        obj = self.set_properties(Fabric(), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_device_chassis_create(self):
        prop_map = {'name': 'dc-%s' % self.id(),
                    'annotations': {},
                    'device_chassis_type': 'fake_chassis_type',
                    'display_name': 'device chassis'}

        obj = self.set_properties(DeviceChassis(), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_device_functional_group_create(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        bridging_roles = RoutingBridgingRolesType(rb_roles=['CRB', 'ERB'])
        prop_map = {'name': 'dfg-%s' % self.id(),
                    'annotations': {},
                    'device_functional_group_description': 'some description',
                    'device_functional_group_os_version': 'junos',
                    'device_functional_group_routing_bridging_roles':
                        bridging_roles,
                    'display_name': 'device functional group'}

        obj = self.set_properties(
            DeviceFunctionalGroup(parent_obj=project), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_hardware_inventory_create(self):
        prop_map = {'name': 'hi-%s' % self.id(),
                    'annotations': {},
                    'display_name': 'hardware inventory',
                    'hardware_inventory_inventory_info':
                        '{"id": 123, "name": "fake"}'}

        pr = PhysicalRouter(name='pr-%s' % self.id(), parent_obj=self.gsc)
        uuid = self.api.physical_router_create(pr)
        pr.set_uuid(uuid)

        obj = self.set_properties(HardwareInventory(parent_obj=pr), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_physical_router_create(self):
        dnsmasq = DnsmasqLeaseParameters(
            lease_expiry_time=100,
            client_id='4d972916-3204-11ea-a6fa-d7a3d77d36a2')
        auto_system = AutonomousSystemsType(asn=[294967295])

        prop_map = {'name': 'pr-%s' % self.id(),
                    'annotations': {},
                    'display_name': 'some_string',
                    'physical_router_autonomous_system': auto_system,
                    'physical_router_dataplane_ip': 'some_string',
                    'physical_router_device_family': 'some_string',
                    'physical_router_dhcp_parameters': dnsmasq,
                    'physical_router_encryption_type': 'none',
                    'physical_router_hostname': 'some_string',
                    'physical_router_junos_service_ports': JunosServicePorts(
                        service_port='2010'),
                    'physical_router_lldp': False,
                    'physical_router_loopback_ip': 'some_string',
                    'physical_router_managed_state': 'some_string',
                    'physical_router_management_ip': 'some_string',
                    'physical_router_management_mac': 'some_string',
                    'physical_router_os_version': 'some_string',
                    'physical_router_product_name': 'some_string',
                    'physical_router_replicator_loopback_ip': 'some_string',
                    'physical_router_role': 'some_string',
                    'physical_router_serial_number': 'some_string',
                    'physical_router_snmp': False,
                    'physical_router_snmp_credentials': SNMPCredentials(
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
                        v3_engine_boots='some_string',
                        v3_engine_time='some_string'),
                    'physical_router_supplemental_config': 'some_string',
                    'physical_router_underlay_config': 'some_string',
                    'physical_router_underlay_managed': False,
                    'physical_router_user_credentials': UserCredentials(
                        username='admin',
                        password='admin'),
                    'physical_router_vendor_name': 'some_string',
                    'physical_router_vnc_managed': False,
                    'routing_bridging_roles': RoutingBridgingRolesType(
                        rb_roles='admin'),
                    'telemetry_info': TelemetryStateInfo(
                        server_ip='10.100.0.100',
                        server_port='8080',
                        resource=TelemetryResourceInfo())}

        obj = self.set_properties(
            PhysicalRouter(parent_obj=self.gsc), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_telemetry_profile_create(self):
        prop_map = {'name': 'tp-%s' % self.id(),
                    'annotations': {},
                    'display_name': 'some_string',
                    'telemetry_profile_is_default': False}

        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        obj = self.set_properties(TelemetryProfile(
            name='tp-%s' % self.id(), parent_obj=project), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_storm_control_profile_create(self):
        scp_params = StormControlParameters(
            storm_control_actions=['interface-shutdown'],
            recovery_timeout=30,
            no_unregistered_multicast=True,
            no_registered_multicast=False,
            no_unknown_unicast=False,
            no_multicast=False,
            no_broadcast=False,
            bandwidth_percent=40)

        prop_map = {'name': 'scp-%s' % self.id(),
                    'annotations': {},
                    'display_name': 'some_string',
                    'storm_control_parameters': scp_params}

        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        obj = self.set_properties(
            StormControlProfile(parent_obj=project), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_sflow_profile_create(self):
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

        prop_map = {'name': 'sfp-%s' % self.id(),
                    'annotations': {},
                    'display_name': 'sflow profile',
                    'sflow_parameters': sfp_params,
                    'sflow_profile_is_default': False}

        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        obj = self.set_properties(SflowProfile(parent_obj=project), prop_map)
        self.assertSchemaObjCreated(obj)

    def test_routing_instance_create(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        ri_name = 'ri-%s' % self.id()
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

        prop_map = {'name': ri_name,
                    'annotations': {},
                    'default_ce_protocol': DefaultProtocolType(
                        bgp=ProtocolBgpType(autonomous_system=42),
                        ospf=ProtocolOspfType(area=1)),
                    'display_name': 'some_string',
                    'evpn_ipv6_service_chain_information': sciv6,
                    'evpn_service_chain_information': sci,
                    'ipv6_service_chain_information': sciv6,
                    'routing_instance_fabric_snat': False,
                    'routing_instance_has_pnf': False,
                    'routing_instance_is_default': False,
                    'service_chain_information': sci,
                    'static_route_entries': StaticRouteEntriesType(
                        route=StaticRouteType(prefix='test',
                                              next_hop='10.100.100.100',
                                              route_target='test',
                                              community=''))}

        obj = self.set_properties(RoutingInstance(parent_obj=vn), prop_map)
        self.assertSchemaObjCreated(obj)
