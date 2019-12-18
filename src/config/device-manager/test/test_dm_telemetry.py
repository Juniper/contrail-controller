#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
import gevent
from attrdict import AttrDict
from .test_dm_ansible_common import TestAnsibleCommonDM
from vnc_api.vnc_api import *


class TestAnsibleTelemetryDM(TestAnsibleCommonDM):

    def test_01_sflow_profile_create_and_update(self):

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_upd_2'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'access'

        self.create_feature_objects_and_params()

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id, enbld_intf_type=enbld_intf_type)
        tm_obj = self.create_telemetry_profile('TP_2', sf_obj)

        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, _, _ = self.create_telemetry_dependencies()

        # Now link tm_obj to pr1

        pr1.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr1)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        tm_obj_fqname = tm_obj.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry',{}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list[-1].name.replace('_', ':'))
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # now update the sflow profile object and check if
        # update causes device abstract config also to update

        sf_params_list = SflowParameters(
            adaptive_sample_rate=500,
            enabled_interface_type='fabric')

        sf_obj.set_sflow_parameters(sf_params_list)
        self._vnc_lib.sflow_profile_update(sf_obj)

        # Now check the changes in the device abstract config
        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 500)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         'fabric')
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list[1].name)
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # delete workflow

        self.delete_objects()

    def test_02_disassociate_sflow_profile_from_telemetry_profile(self):

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_disassociate_telemetry'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'service'

        self.create_feature_objects_and_params()

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id, enbld_intf_type=enbld_intf_type)
        tm_obj = self.create_telemetry_profile('TP_disassociate', sf_obj)

        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, _, _ = self.create_telemetry_dependencies()

        # Now link tm_obj to pr1

        pr1.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr1)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        tm_obj_fqname = tm_obj.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry',{}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list[0].name)
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # Now disassociate sflow from telemetry profile
        tm_obj.del_sflow_profile(sf_obj)
        self._vnc_lib.telemetry_profile_update(tm_obj)

        # Now check the changes in the device abstract config
        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('telemetry', [])

        self.assertEqual(telemetry_profiles, [])

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertEqual(phy_interfaces, [])

        # delete workflow

        self.delete_objects()

    def test_03_sflow_custom_diff_devices_same_interface_names(self):

        self.create_feature_objects_and_params()
        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, pr2, intf_obj_list_2 = self.create_telemetry_dependencies(
            phy_router_no=2
        )

        intf_1_fq_name_str = ':'.join(intf_obj_list[0].get_fq_name())
        intf_2_fq_name_str = ':'.join(intf_obj_list[1].get_fq_name())

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_custom_intf_telemetry'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'custom'
        enbld_intf_params = [
            {
                "stats_collection_frequency": {
                    "sample_rate": 9000,
                    "polling_interval": 700
                },
                "name": intf_1_fq_name_str
            },
            {
                "name": intf_2_fq_name_str
            }
        ]

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id,
                                           enbld_intf_type=enbld_intf_type,
                                           enbld_intf_params=enbld_intf_params)

        tm_obj = self.create_telemetry_profile('TP_custom_intf', sf_obj)

        tm_obj_fqname = tm_obj.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()

        # Now link tm_obj to pr1 and pr2

        pr1.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr1)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('telemetry', [])

        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNotNone(sflow_profile.get('enabled_interface_params'))
        sflow_enbld_intf_params = sflow_profile.get('enabled_interface_params')
        self.assertEqual(len(sflow_enbld_intf_params), 2)

        for sflow_enbld_intf_param in sflow_enbld_intf_params:
            if sflow_enbld_intf_param.get('name') == intf_1_fq_name_str:
                self.assertEqual(sflow_enbld_intf_param.get('sample_rate'), 9000)
                self.assertEqual(sflow_enbld_intf_param.get('polling_interval'), 700)
            elif sflow_enbld_intf_param.get('name') == intf_2_fq_name_str:
                self.assertIsNone(sflow_enbld_intf_param.get('sample_rate'))
                self.assertIsNone(sflow_enbld_intf_param.get('polling_interval'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertEqual(len(phy_interfaces), 2)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
            self.assertIn(phy_intf.get('name').replace(':', '_'),
                          [intf_obj_list[0].name,
                           intf_obj_list[1].name])

        pr2.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr2)

        # Now check the changes in the device abstract config
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('telemetry', [])

        self.assertEqual(telemetry_profiles, [])

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertEqual(phy_interfaces, [])

        # delete workflow

        self.delete_objects()

    def test_04_sflow_profile_with_collector(self):

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_upd_collector'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'access'

        self.create_feature_objects_and_params()

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id, enbld_intf_type=enbld_intf_type)
        tm_obj = self.create_telemetry_profile('TP_collector', sf_obj)

        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, _, _ = self.create_telemetry_dependencies()

        # Now create collector nodes so as to come in
        # device abstract config
        flow_node_obj = FlowNode()
        flow_node_obj.set_flow_node_load_balancer_ip('5.5.5.5')
        self._vnc_lib.flow_node_create(flow_node_obj)

        # Now link tm_obj to pr1

        pr1.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr1)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        tm_obj_fqname = tm_obj.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry',{}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNotNone(sflow_profile.get('collector_params'))
        self.assertEqual(sflow_profile.get('collector_params').get('ip_address'), '5.5.5.5')
        self.assertEqual(sflow_profile.get('collector_params').get('udp_port'), 6343)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list[-1].name.replace('_', ':'))
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # delete workflow

        self.delete_objects()

    def test_05_multiple_pr_same_telemetry(self):

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_multiple_pr_one_tp'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'fabric'

        self.create_feature_objects_and_params()

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id, enbld_intf_type=enbld_intf_type)
        tm_obj = self.create_telemetry_profile('TP_multi_pr', sf_obj)
        tm_obj_fqname = tm_obj.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()

        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, pr2, intf_obj_list_2 = self.create_telemetry_dependencies(
            phy_router_no=2
        )

        # Now link tm_obj to pr1 and pr2

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        pr1.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr1)

        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        pr2.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr2)

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list_2[1].name.replace('_', ':'))
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # Verify if telemetry profiles are still attached

        pr1.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr1)

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry',{}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list[1].name.replace('_', ':'))
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # delete workflow

        self.delete_objects()

    def test_06_multiple_sflow_backrefs_same_telemetry(self):

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_backrefs_tp'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'service'

        self.create_feature_objects_and_params()

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id, enbld_intf_type=enbld_intf_type)
        tm_obj_1 = self.create_telemetry_profile('TP_1', sf_obj)
        tm_obj_2 = self.create_telemetry_profile('TP_2', sf_obj)

        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, pr2, intf_obj_list_2 = self.create_telemetry_dependencies(
                phy_router_no=2
            )

        # Now link tm_obj_1 to pr1

        pr1.set_telemetry_profile(tm_obj_1)
        self._vnc_lib.physical_router_update(pr1)

        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        # Now link tm_obj_2 to pr2

        pr2.set_telemetry_profile(tm_obj_2)
        self._vnc_lib.physical_router_update(pr2)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        tm_obj1_fqname = tm_obj_1.get_fq_name()
        tm_obj2_fqname = tm_obj_2.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry',{}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj2_fqname[-1] + "-" + tm_obj2_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list_2[0].name.replace('_', ':'))
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj2_fqname[-1] + "-" + tm_obj2_fqname[-2])

        pr1.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr1)

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj1_fqname[-1] + "-" + tm_obj1_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertIsNotNone(phy_interfaces)
        for phy_intf in phy_interfaces:
            self.assertEqual(phy_intf.get('name'), intf_obj_list[0].name.replace('_', ':'))
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj1_fqname[-1] + "-" + tm_obj1_fqname[-2])

        # delete workflow

        self.delete_objects()

    def test_07_sflow_all_interfaces(self):

        # check if sample rt, polling interval and adaptive sample rate
        # is set to their default values.
        # create objects

        sf_name = 'sflow_upd_collector'
        agent_id = '10.0.0.1'
        enbld_intf_type = 'all'

        self.create_feature_objects_and_params()

        sf_obj = self.create_sflow_profile(sf_name, agent_id=agent_id, enbld_intf_type=enbld_intf_type)
        tm_obj = self.create_telemetry_profile('TP_all', sf_obj)

        # intf_obj_list will be list of 3 types of interfaces
        # in the order xe-0/0/0 - service
        # xe-0/0/1 - fabric and xe-0/0/2:0 - access
        pr1, intf_obj_list, _, _ = self.create_telemetry_dependencies()

        # Now link tm_obj to pr1

        pr1.set_telemetry_profile(tm_obj)
        self._vnc_lib.physical_router_update(pr1)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        tm_obj_fqname = tm_obj.get_fq_name()
        sf_obj_fqname = sf_obj.get_fq_name()
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get(
            'device_abstract_config')

        # Will be a list of one telemetry profile for a physical
        # router
        telemetry_profiles = device_abstract_config.get(
            'features', {}).get('telemetry',{}).get('telemetry', [])
        telemetry_profile = telemetry_profiles[-1]
        sflow_profile = telemetry_profile.get('sflow_profile')

        self.assertIsNotNone(sflow_profile)
        self.assertEqual(telemetry_profile.get('name'),
                         tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])
        self.assertEqual(sflow_profile.get('name'),
                         sf_obj_fqname[-1] + "-" + sf_obj_fqname[-2])

        self.assertEqual(sflow_profile.get('sample_rate'), 2000)
        self.assertEqual(sflow_profile.get('adaptive_sample_rate'), 300)
        self.assertEqual(sflow_profile.get('polling_interval'), 20)
        self.assertEqual(sflow_profile.get('agent_id'), agent_id)
        self.assertEqual(sflow_profile.get('enabled_interface_type'),
                         enbld_intf_type)
        self.assertIsNone(sflow_profile.get('enabled_interface_params'))

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('telemetry', {}).get('physical_interfaces', [])

        self.assertEqual(len(phy_interfaces), 3)

        # create a list of intf_obj_list_names
        intf_obj_list_names = []
        for intf_obj in intf_obj_list:
            intf_obj_list_names.append(intf_obj.name.replace('_', ':'))

        for phy_intf in phy_interfaces:
            self.assertIn(phy_intf.get('name'), intf_obj_list_names)
            self.assertEqual(phy_intf.get('telemetry_profile'),
                             tm_obj_fqname[-1] + "-" + tm_obj_fqname[-2])

        # delete workflow

        self.delete_objects()

    def create_telemetry_dependencies(self, phy_router_no=1):

        pi_obj_4 = None
        pi_obj_5 = None
        pr2 = None

        jt = self.create_job_template('job-template-sf' + self.id())

        fabric = self.create_fabric('fab-sf' + self.id())

        np, rc = self.create_node_profile('node-profile-sf' + self.id(),
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'leaf',
                                                   'rb_roles': ['crb-access']}
                                              )],
                                          job_template=jt)

        bgp_router1, pr1 = self.create_router('device-sf1' + self.id(),
                                              '3.3.3.3',
                                              product='qfx5110-48s-4c', family='junos-qfx',
                                              role='leaf', rb_roles=['crb-access'],
                                              physical_role=self.physical_roles['leaf'],
                                              overlay_role=self.overlay_roles[
                                                  'crb-access'], fabric=fabric,
                                              node_profile=np, ignore_bgp=True)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        if phy_router_no != 1:
            bgp_router1, pr2 = self.create_router('device-sf' + str(phy_router_no) + self.id(),
                                                  '3.3.3.4',
                                                  product='qfx5110-48s-4c', family='junos-qfx',
                                                  role='leaf', rb_roles=['crb-access'],
                                                  physical_role=self.physical_roles['leaf'],
                                                  overlay_role=self.overlay_roles[
                                                      'crb-access'], fabric=fabric,
                                                  node_profile=np, ignore_bgp=True)
            pr1.set_physical_router_loopback_ip('30.30.0.2')
            self._vnc_lib.physical_router_update(pr2)

            pi_name = "xe-0/0/0"
            pi_obj_4 = PhysicalInterface(pi_name, parent_obj=pr2)
            pi_obj_4.set_physical_interface_type('service')
            self._vnc_lib.physical_interface_create(pi_obj_4)

            pi_name = "xe-0/0/1"
            pi_obj_5 = PhysicalInterface(pi_name, parent_obj=pr2)
            pi_obj_5.set_physical_interface_type('fabric')
            self._vnc_lib.physical_interface_create(pi_obj_5)

        pi_name = "xe-0/0/0"
        pi_obj_1 = PhysicalInterface(pi_name, parent_obj=pr1)
        pi_obj_1.set_physical_interface_type('service')
        self._vnc_lib.physical_interface_create(pi_obj_1)

        pi_name = "xe-0/0/1"
        pi_obj_2 = PhysicalInterface(pi_name, parent_obj=pr1)
        pi_obj_2.set_physical_interface_type('fabric')
        self._vnc_lib.physical_interface_create(pi_obj_2)

        pi_name = "xe-0/0/2_0"
        pi_obj_3 = PhysicalInterface(pi_name, parent_obj=pr1)
        pi_obj_3.set_physical_interface_type('access')
        self._vnc_lib.physical_interface_create(pi_obj_3)

        return pr1, [pi_obj_1, pi_obj_2, pi_obj_3], pr2, [pi_obj_4, pi_obj_5]

    def create_feature_objects_and_params(self, role='crb-access'):
        self.create_features(['telemetry'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles([role])
        self.create_role_definitions([
            AttrDict({
                'name': 'telemetry-role',
                'physical_role': 'leaf',
                'overlay_role': role,
                'features': ['telemetry'],
                'feature_configs': None
            })
        ])

    def delete_objects(self):

        pi_list = self._vnc_lib.physical_interfaces_list().get('physical-interfaces')
        for pi in pi_list:
            self._vnc_lib.physical_interface_delete(id=pi['uuid'])

        pr_list = self._vnc_lib.physical_routers_list().get('physical-routers')
        for pr in pr_list:
            self._vnc_lib.physical_router_delete(id=pr['uuid'])

        rc_list = self._vnc_lib.role_configs_list().get('role-configs')
        for rc in rc_list:
            self._vnc_lib.role_config_delete(id=rc['uuid'])

        np_list = self._vnc_lib.node_profiles_list().get('node-profiles')
        for np in np_list:
            self._vnc_lib.node_profile_delete(id=np['uuid'])

        fab_list = self._vnc_lib.fabrics_list().get('fabrics')
        for fab in fab_list:
            self._vnc_lib.fabric_delete(id=fab['uuid'])

        jt_list = self._vnc_lib.job_templates_list().get('job-templates')
        for jt in jt_list:
            self._vnc_lib.job_template_delete(id=jt['uuid'])

        tm_list = self._vnc_lib.telemetry_profiles_list().get('telemetry-profiles')
        for tm in tm_list:
            self._vnc_lib.telemetry_profile_delete(id=tm['uuid'])

        sf_list = self._vnc_lib.sflow_profiles_list().get('sflow-profiles')
        for sf in sf_list:
            self._vnc_lib.sflow_profile_delete(id=sf['uuid'])

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
