#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging

from vnc_api.vnc_api import AutonomousSystemsType
from vnc_api.vnc_api import DeviceChassis
from vnc_api.vnc_api import DeviceFunctionalGroup
from vnc_api.vnc_api import DnsmasqLeaseParameters
from vnc_api.vnc_api import EnabledInterfaceParams
from vnc_api.vnc_api import ExecutableInfoListType, ExecutableInfoType
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import HardwareInventory
from vnc_api.vnc_api import JobTemplate
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import RoutingBridgingRolesType
from vnc_api.vnc_api import RoutingInstance
from vnc_api.vnc_api import ServiceChainInfo
from vnc_api.vnc_api import SflowParameters, SflowProfile
from vnc_api.vnc_api import StormControlParameters, StormControlProfile
from vnc_api.vnc_api import StatsCollectionFrequency
from vnc_api.vnc_api import TelemetryProfile
from vnc_api.vnc_api import VirtualNetwork
from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class InPlaceConfigTestCase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(InPlaceConfigTestCase, cls).setUpClass(*args, **kwargs)
        cls.load_db_contents()

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        cls.dump_db_contents()
        super(InPlaceConfigTestCase, cls).tearDownClass()

    def setUp(self):
        super(InPlaceConfigTestCase, self).setUp()
        gsc_fq_name = GlobalSystemConfig().fq_name
        self.gsc = self.api.global_system_config_read(gsc_fq_name)

    @property
    def api(self):
        return self._vnc_lib

    @staticmethod
    def set_properties(obj, prop_map):
        """Set values to object using properties map.

        For every property which allow for 'Create' operation,
        set a value from prop_map.

        :param obj: schema resource
        :param prop_map: dict
        :return: schema resource
        """
        prop_not_found = []
        for prop, info in obj.prop_field_types.items():
            if 'C' in info['operations']:
                if prop not in prop_map:
                    prop_not_found.append(prop)
                    continue

                set_method = getattr(obj, 'set_%s' % prop)
                set_method(prop_map[prop])
        if len(prop_not_found) > 0:
            raise Exception(
                'Properties nod defined in prop_map: '
                '{} for object: {}'.format(', '.join(prop_not_found),
                                           obj.__class__.__name__))
        return obj


class TestFabricObjects(InPlaceConfigTestCase):
    def test_job_template_create(self):
        exe_info_list = ExecutableInfoListType()
        exe_info_list.set_executable_info([
            ExecutableInfoType(executable_path='/tmp/fake',
                               executable_args='a b c',
                               job_completion_weightage=10)])
        prop_map = {
            'name': 'jt-%s' % self.id(),
            'display_name': 'job template test',
            'parent_obj': self.gsc,
            'job_template_executables': exe_info_list,
            'job_template_output_schema': None,
            'job_template_output_ui_schema': None,
            'job_template_concurrency_level': None,
            'job_template_description': None,
            'job_template_type': None,
            'job_template_input_ui_schema': None,
            'job_template_synchronous_job': None,
            'job_template_input_schema': None,
            'job_template_playbooks': None,
            'annotations': None,
        }
        jt = self.set_properties(JobTemplate(), prop_map)
        uuid = self.api.job_template_create(jt)
        self.assertIsNotNone(uuid)

    # def test_fabric_basic_crud(self):
    #     f = Fabric(name='f-%s' % self.id(), parent_obj=self.gsc)
    #     f.set_fabric_os_version('junos')
    #     f.set_fabric_enterprise_style(False)
    #     uuid = self.api.fabric_create(f)
    #     f.set_uuid(uuid)
    #
    #     f.set_fabric_os_version('prioprietary')
    #     self.api.fabric_update(f)
    #     f_updated = self.api.fabric_read(id=f.uuid)
    #
    #     for attr in ['fabric_os_version', 'fabric_enterprise_style']:
    #         self.assertEqual(getattr(f, attr), getattr(f_updated, attr))
    #
    # def test_device_chassis_basic_crud(self):
    #     dc = DeviceChassis(name='dc-%s' % self.id())
    #     dc.set_device_chassis_type('fake_chassis_type')
    #     uuid = self.api.device_chassis_create(dc)
    #     dc.set_uuid(uuid)
    #     dc.set_device_chassis_type('other_chassis_type')
    #     self.api.device_chassis_update(dc)
    #
    #     dc_updated = self.api.device_chassis_read(id=dc.uuid)
    #     self.assertEqual(dc.device_chassis_type,
    #                      dc_updated.device_chassis_type)
    #
    # def test_device_functional_group(self):
    #     project = Project('project-%s' % self.id())
    #     self.api.project_create(project)
    #
    #     dfg = DeviceFunctionalGroup(parent_obj=project)
    #     dfg.set_device_functional_group_description('some description')
    #     dfg.set_device_functional_group_os_version('junos')
    #
    #     bridging_roles = RoutingBridgingRolesType(rb_roles=['CRB', 'ERB'])
    #     dfg.set_device_functional_group_routing_bridging_roles(bridging_roles)
    #
    #     uuid = self.api.device_functional_group_create(dfg)
    #     dfg.set_uuid(uuid)
    #
    #     dfg.set_device_functional_group_os_version('proprietary')
    #     self.api.device_functional_group_update(dfg)
    #     dfg_updated = self.api.device_functional_group_read(id=dfg.uuid)
    #
    #     self.assertEqual(dfg.device_functional_group_os_version,
    #                      dfg_updated.device_functional_group_os_version)
    #
    #     rb1 = dfg \
    #         .get_device_functional_group_routing_bridging_roles() \
    #         .get_rb_roles()
    #     rb2 = dfg_updated \
    #         .get_device_functional_group_routing_bridging_roles() \
    #         .get_rb_roles()
    #     self.assertListEqual(rb1, rb2)
    #
    # def test_hardware_inventory_basic_crud(self):
    #     pr = PhysicalRouter(name='pr-%s' % self.id(), parent_obj=self.gsc)
    #     uuid = self.api.physical_router_create(pr)
    #     pr.set_uuid(uuid)
    #
    #     hi = HardwareInventory(name='hi-%s' % self.id(), parent_obj=pr)
    #     hi.set_hardware_inventory_inventory_info('{"id": 123, "name": "fake"}')
    #
    #     uuid = self.api.hardware_inventory_create(hi)
    #     hi.set_uuid(uuid)
    #
    #     hi.set_hardware_inventory_inventory_info('{"id": 456, "name": "fake"}')
    #     self.api.hardware_inventory_update(hi)
    #
    #     hi_updated = self.api.hardware_inventory_read(id=hi.uuid)
    #
    #     self.assertEqual(hi.hardware_inventory_inventory_info,
    #                      hi_updated.hardware_inventory_inventory_info)
    #
    # def test_physical_router_basic_crud(self):
    #     pr = PhysicalRouter(name='pr-%s' % self.id(), parent_obj=self.gsc)
    #     pr.set_physical_router_encryption_type('none')
    #     pr.set_physical_router_dhcp_parameters(DnsmasqLeaseParameters(
    #         lease_expiry_time=100,
    #         client_id='4d972916-3204-11ea-a6fa-d7a3d77d36a2'))
    #     # TODO: obj has no attribute error, but it is defined.
    #     # pr.set_physical_router_supplemental_config('dummy config')
    #     pr.set_physical_router_autonomous_system(
    #         AutonomousSystemsType(asn=[294967295]))
    #
    #     uuid = self.api.physical_router_create(pr)
    #     pr.set_uuid(uuid)
    #
    #     pr.set_physical_router_encryption_type('local')
    #     pr.set_physical_router_autonomous_system(
    #         AutonomousSystemsType(asn=[967295]))
    #     self.api.physical_router_update(pr)
    #     pr_updated = self.api.physical_router_read(id=pr.uuid)
    #
    #     self.assertEqual(pr.physical_router_encryption_type,
    #                      pr_updated.physical_router_encryption_type)
    #     self.assertEqual(pr.physical_router_autonomous_system.asn,
    #                      pr_updated.physical_router_autonomous_system.asn)
    #
    #     # TODO: obj has no attribute error, but it is defined.
    #     # self.assertEqual(pr.physical_router_supplemental_config,
    #     #                  pr_updated.physical_router_supplemental_config)
    #
    # def test_telemetry_profile_basic_crud(self):
    #     project = Project('project-%s' % self.id())
    #     self.api.project_create(project)
    #
    #     tp = TelemetryProfile(name='tp-%s' % self.id(), parent_obj=project)
    #     tp.set_telemetry_profile_is_default(False)
    #
    #     uuid = self.api.telemetry_profile_create(tp)
    #     tp.set_uuid(uuid)
    #     tp.set_display_name('tp-updated-name')
    #
    #     self.api.telemetry_profile_update(tp)
    #     tp_updated = self.api.telemetry_profile_read(id=tp.uuid)
    #
    #     self.assertFalse(tp_updated.telemetry_profile_is_default)
    #     self.assertEqual(tp.display_name, tp_updated.display_name)
    #
    # def test_storm_control_profile_basic_crud(self):
    #     project = Project('project-%s' % self.id())
    #     self.api.project_create(project)
    #
    #     scp = StormControlProfile(name='scp-%s' % self.id(),
    #                               parent_obj=project)
    #
    #     # create storm control parameters
    #     scp_params = StormControlParameters(
    #         storm_control_actions=['interface-shutdown'],
    #         recovery_timeout=30,
    #         no_unregistered_multicast=True,
    #         no_registered_multicast=False,
    #         no_unknown_unicast=False,
    #         no_multicast=False,
    #         no_broadcast=False,
    #         bandwidth_percent=40)
    #     scp.set_storm_control_parameters(scp_params)
    #
    #     uuid = self.api.storm_control_profile_create(scp)
    #     scp.set_uuid(uuid)
    #
    #     # update storm control parameters
    #     scp_params.set_recovery_timeout(90)
    #     scp_params.set_bandwidth_percent(100)
    #     scp_params.set_no_unregistered_multicast(False)
    #     scp_params.set_no_multicast(True)
    #     scp.set_storm_control_parameters(scp_params)
    #
    #     self.api.storm_control_profile_update(scp)
    #     scp_updated = self.api.storm_control_profile_read(id=scp.uuid)
    #
    #     scp_param1 = scp.storm_control_parameters
    #     scp_param2 = scp_updated.storm_control_parameters
    #     for attr in ['storm_control_actions',
    #                  'recovery_timeout',
    #                  'no_unregistered_multicast',
    #                  'no_registered_multicast',
    #                  'no_unknown_unicast',
    #                  'no_multicast',
    #                  'no_broadcast',
    #                  'bandwidth_percent']:
    #         self.assertEqual(getattr(scp_param1, attr),
    #                          getattr(scp_param2, attr))
    #
    # def test_sflow_profile_basic_crud(self):
    #     project = Project('project-%s' % self.id())
    #     self.api.project_create(project)
    #
    #     sfp = SflowProfile(name='sfp-%s' % self.id(), parent_obj=project)
    #     sfp.set_sflow_profile_is_default(False)
    #
    #     stats_collection_freq = StatsCollectionFrequency(sample_rate=42,
    #                                                      polling_interval=10,
    #                                                      direction='ingress')
    #     sfp_params = SflowParameters(
    #         stats_collection_frequency=stats_collection_freq,
    #         agent_id='10.100.150.126',
    #         adaptive_sample_rate=500,
    #         enabled_interface_type='custom',
    #         enabled_interface_params=[EnabledInterfaceParams(
    #             name='default-interface-params',
    #             stats_collection_frequency=stats_collection_freq,
    #         )])
    #     sfp.set_sflow_parameters(sfp_params)
    #
    #     uuid = self.api.sflow_profile_create(sfp)
    #     sfp.set_uuid(uuid)
    #
    #     sfp_params.set_agent_id('10.105.100.26')
    #     sfp_params.set_adaptive_sample_rate(900)
    #     sfp_params.set_enabled_interface_type('all')
    #     sfp.set_sflow_parameters(sfp_params)
    #
    #     self.api.sflow_profile_update(sfp)
    #     sfp_updated = self.api.sflow_profile_read(id=sfp.uuid)
    #
    #     # assert sflow attrs
    #     for attr in ['agent_id',
    #                  'adaptive_sample_rate',
    #                  'enabled_interface_type']:
    #         self.assertEqual(getattr(sfp.sflow_parameters, attr),
    #                          getattr(sfp_updated.sflow_parameters, attr))
    #
    #     sfp_params = sfp_updated.sflow_parameters
    #     self.assertEqual(stats_collection_freq,
    #                      sfp_params.stats_collection_frequency)
    #
    #     iface_params = sfp_params.enabled_interface_params[0]
    #     self.assertEqual(stats_collection_freq,
    #                      iface_params.stats_collection_frequency)
    #
    # def test_routing_instance_service_chain_info(self):
    #     project = Project('project-%s' % self.id())
    #     self.api.project_create(project)
    #     vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
    #     self.api.virtual_network_create(vn)
    #
    #     ri_name = 'ri-%s' % self.id()
    #     ri_fq_name = ':'.join(vn.fq_name + [ri_name])
    #
    #     sci = ServiceChainInfo(
    #         service_chain_id=ri_fq_name,
    #         prefix=['20.0.0.0/24'],
    #         routing_instance=ri_name,
    #         service_chain_address='0.255.255.250',
    #         service_instance='default-domain:default-project:test_service',
    #         sc_head=True)
    #
    #     sciv6 = ServiceChainInfo(
    #         service_chain_id=ri_fq_name,
    #         prefix=['1000::/16'],
    #         routing_instance=ri_name,
    #         service_chain_address='::0.255.255.252',
    #         service_instance='default-domain:default-project:test_service_v6',
    #         sc_head=False)
    #
    #     ri = RoutingInstance(name=ri_name,
    #                          parent_obj=vn,
    #                          service_chain_information=sci,
    #                          ipv6_service_chain_information=sciv6,
    #                          evpn_service_chain_information=sci,
    #                          evpn_ipv6_service_chain_information=sciv6,
    #                          routing_instance_is_default=False)
    #
    #     uuid = self.api.routing_instance_create(ri)
    #     ri.set_uuid(uuid)
    #     ri_fq_name = vn.fq_name + [ri.name]
    #     ri = self.api.routing_instance_read(ri_fq_name)
    #     ri.set_display_name('new RI name')
    #     self.api.routing_instance_update(ri)
    #
    #     updated_ri = self.api.routing_instance_read(id=ri.uuid)
    #     for attr in ['service_chain_information',
    #                  'ipv6_service_chain_information',
    #                  'evpn_service_chain_information',
    #                  'evpn_ipv6_service_chain_information']:
    #         self.assertEqual(getattr(ri, attr), getattr(updated_ri, attr))
