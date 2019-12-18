#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
import gevent
import mock
from unittest import skip
from attrdict import AttrDict
from vnc_api.vnc_api import *
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler


class TestAnsibleStormControlDM(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleStormControlDM, self).setUp(extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()
        self.create_feature_objects_and_params()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestAnsibleStormControlDM, self).tearDown()
        self.delete_objects()

    def test_01_storm_control_profile_update(self):
        # create objects

        sc_name = 'strm_ctrl_upd'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies()
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        sc_obj_fqname = sc_obj.get_fq_name()
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')
        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        # now update the storm control profile object and check if
        # update causes device abstract config also to update

        sc_params_list = StormControlParameters(
            recovery_timeout=1200,
            bandwidth_percent=40)

        sc_obj.set_storm_control_parameters(sc_params_list)
        self._vnc_lib.storm_control_profile_update(sc_obj)

        # Now check the changes in the device abstract config
        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), 40)
        self.assertEqual(storm_control_profile.get('actions'), None)
        self.assertEqual(storm_control_profile.get('traffic_type'), None)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), 1200)

    def test_02_port_profile_vmi_association(self):
        # create objects

        sc_name = 'strm_ctrl_vmi'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies()
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj)


        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        sc_obj_fqname = sc_obj.get_fq_name()
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/0" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

    def test_03_port_profile_service_provider_style_crb_access(self):
        # create objects

        sc_name = 'strm_ctrl_sp_style_crb'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        self.create_overlay_roles(['crb-access'])

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies(enterprise_style=False, role='crb-access')
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get('device_abstract_config')
        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])

        self.assertEqual(storm_control_profiles, [])

    def test_04_disassociate_PP_from_VMI(self):
        sc_name = 'strm_ctrl_pp'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies()
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj)


        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        sc_obj_fqname = sc_obj.get_fq_name()
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        # now disassociate the port profile from VMI

        vmi_obj.set_port_profile_list([])
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])

        self.assertEqual(storm_control_profiles, [])


        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/0" in log_intf.get('name'):
                    self.assertIsNone(log_intf.get('storm_control_profile'))

    def test_05_port_profile_service_provider_style_erb_ucast(self):
        # create objects

        sc_name = 'strm_ctrl_sp_style_erb'
        bw_percent = 47
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies(enterprise_style=False)
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj)


        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get('device_abstract_config')
        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])

        storm_control_profile = storm_control_profiles[-1]
        sc_obj_fqname = sc_obj.get_fq_name()
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/0" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

    # TBD: Please enable test_06 once VPG for scale setup issue has been resolved
    @skip("Timing failures")
    def test_06_port_profile_multiple_vpgs_same_vlan(self):
        # create objects

        sc_name_1 = 'strm_ctrl_multi_vpg_1'
        sc_name_2 = 'strm_ctrl_multi_vpg_2'
        bw_percent_1 = 47
        bw_percent_2 = 52
        traffic_type_1 = ['no-broadcast', 'no-multicast']
        actions_1 = ['interface-shutdown']
        traffic_type_2 = ['no-registered-multicast', 'no-unknown-unicast']

        sc_obj_1 = self.create_storm_control_profile(sc_name_1, bw_percent_1, traffic_type_1, actions_1, recovery_timeout=900)
        pp_obj_1 = self.create_port_profile('port_profile_vmi_1', sc_obj_1)

        sc_obj_2 = self.create_storm_control_profile(sc_name_2, bw_percent_2, traffic_type_2, None)
        pp_obj_2 = self.create_port_profile('port_profile_vmi_2', sc_obj_2)

        sc_obj1_fqname = sc_obj_1.get_fq_name()
        sc_obj2_fqname = sc_obj_2.get_fq_name()

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies()
        vmi_obj_1 = self.create_vpg_and_vmi(pp_obj_1, pr1, fabric, pi_obj_1, vn_obj, pp_obj_2)
        vmi_obj_2 = self.create_vpg_and_vmi(pp_obj_2, pr1, fabric, pi_obj_2, vn_obj, pp_obj_2=pp_obj_1, vpg_nm=2)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get('device_abstract_config')
        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        self.assertEqual(len(storm_control_profiles), 2)
        for storm_control_profile in storm_control_profiles:
            if storm_control_profile.get('name') == sc_obj1_fqname[-1] + "-" + sc_obj1_fqname[-2]:
                self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent_1)
                self.assertEqual(storm_control_profile.get('actions'), actions_1)
                self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type_1)
                self.assertEqual(storm_control_profile.get('recovery_timeout'), 900)
            else:
                self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent_2)
                self.assertEqual(storm_control_profile.get('actions'), None)
                self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type_2)
                self.assertEqual(storm_control_profile.get('recovery_timeout'), None)


        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/0" in log_intf.get('name'):
                    if log_intf.get('unit') == '10':
                        self.assertEqual(log_intf.get('storm_control_profile'),
                                         sc_obj1_fqname[-1] + "-" + sc_obj1_fqname[-2])
                    else:
                        self.assertEqual(log_intf.get('storm_control_profile'),
                                         sc_obj2_fqname[-1] + "-" + sc_obj2_fqname[-2])
                elif "xe-0/0/1" in log_intf.get('name'):
                    if log_intf.get('unit') == '10':
                        self.assertEqual(log_intf.get('storm_control_profile'),
                                         sc_obj2_fqname[-1] + "-" + sc_obj2_fqname[-2])
                    else:
                        self.assertEqual(log_intf.get('storm_control_profile'),
                                         sc_obj1_fqname[-1] + "-" + sc_obj1_fqname[-2])

    def test_07_vpg_lag(self):
        # create objects

        sc_name = 'strm_ctrl_lag'
        bw_percent = 25
        traffic_type = ['no-broadcast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies()
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj,
                                          pi_obj2=pi_obj_2)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        sc_obj_fqname = sc_obj.get_fq_name()
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "ae" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

    def test_08_vpg_mh(self):
        # create objects

        sc_name = 'strm_ctrl_mh'
        bw_percent = 29
        traffic_type = ['no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        pr1, fabric, pi_obj_1, _, vn_obj, pr2, pi_obj_1_pr2 = self.create_vpg_dependencies(mh=True)
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj,
                                          pi_obj2=pi_obj_1_pr2, pr2=pr2)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        # create a dummy product_name just to cause a config push
        pr1.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr1)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config_1 = abstract_config.get('device_abstract_config')

        # create a dummy product_name just to cause a config push
        pr2.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr2)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config_2 = abstract_config.get('device_abstract_config')

        self.assertEqual(device_abstract_config_1.get('system', {}).get(
            'management_ip'), "3.3.3.3")
        storm_control_profiles = device_abstract_config_1.get(
            'features', {}).get('storm-control', {}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        sc_obj_fqname = sc_obj.get_fq_name()
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        phy_interfaces = device_abstract_config_1.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "ae" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

        self.assertEqual(device_abstract_config_2.get('system', {}).get(
            'management_ip'), "3.3.3.2")
        storm_control_profiles = device_abstract_config_2.get(
            'features', {}).get('storm-control', {}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        phy_interfaces = device_abstract_config_2.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "ae" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

    def test_09_vpg_mh_and_single(self):
        # create objects

        sc_name = 'strm_ctrl_mh_and_single'
        bw_percent = 43
        traffic_type = ['no-broadcast']
        actions = ['interface-shutdown']

        sc_name_2 = 'strm_ctrl_2'
        bw_percent_2 = 52
        traffic_type_2 = ['no-registered-multicast', 'no-unknown-unicast']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=900)
        pp_obj = self.create_port_profile('port_profile_vmi', sc_obj)

        sc_obj_2 = self.create_storm_control_profile(sc_name_2, bw_percent_2, traffic_type_2, None)
        pp_obj_2 = self.create_port_profile('port_profile_vmi_2', sc_obj_2)

        sc_obj_fqname = sc_obj.get_fq_name()
        sc_obj2_fqname = sc_obj_2.get_fq_name()

        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, pr2, pi_obj_1_pr2 = self.create_vpg_dependencies(mh=True)
        vmi_obj = self.create_vpg_and_vmi(pp_obj, pr1, fabric, pi_obj_1, vn_obj,
                                          pi_obj2=pi_obj_1_pr2, pr2=pr2)
        vmi_obj = self.create_vpg_and_vmi(pp_obj_2, pr1, fabric,
                                          pi_obj_2, vn_obj, vpg_nm=2)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        # create a dummy product_name just to cause a config push
        pr1.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr1)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config_1 = abstract_config.get('device_abstract_config')

        # create a dummy product_name just to cause a config push
        pr2.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr2)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config_2 = abstract_config.get('device_abstract_config')

        self.assertEqual(device_abstract_config_1.get('system', {}).get(
            'management_ip'), "3.3.3.3")

        storm_control_profiles = device_abstract_config_1.get(
            'features', {}).get('storm-control', {}).get('storm_control', [])

        self.assertEqual(len(storm_control_profiles), 2)
        for storm_control_profile in storm_control_profiles:
            if storm_control_profile.get('name') == sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2]:
                self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
                self.assertEqual(storm_control_profile.get('actions'), actions)
                self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
                self.assertEqual(storm_control_profile.get('recovery_timeout'), 900)
            else:
                self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent_2)
                self.assertEqual(storm_control_profile.get('actions'), None)
                self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type_2)
                self.assertEqual(storm_control_profile.get('recovery_timeout'), None)

        phy_interfaces = device_abstract_config_1.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/1" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj2_fqname[-1] + "-" + sc_obj2_fqname[-2])
                else:
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

        self.assertEqual(device_abstract_config_2.get('system', {}).get(
            'management_ip'), "3.3.3.2")
        storm_control_profiles = device_abstract_config_2.get(
            'features', {}).get('storm-control', {}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        sc_obj_fqname = sc_obj.get_fq_name()
        self.assertEqual(storm_control_profile.get('name'),
                         sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), 900)

        phy_interfaces = device_abstract_config_2.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "ae" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'),
                                     sc_obj_fqname[-1] + "-" + sc_obj_fqname[-2])

    def create_feature_objects_and_params(self, role='erb-ucast-gateway'):
        self.create_features(['storm-control'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles([role])
        self.create_role_definitions([
            AttrDict({
                'name': 'storm-control-role',
                'physical_role': 'leaf',
                'overlay_role': role,
                'features': ['storm-control'],
                'feature_configs': None
            })
        ])

    def create_vpg_dependencies(self, enterprise_style=True,
                                role='erb-ucast-gateway', mh=False):

        pr2 = None
        pi_obj_1_pr2 = None
        jt = self.create_job_template('job-template-sc' + self.id())

        fabric = self.create_fabric('fab-sc' + self.id())
        fabric.set_fabric_enterprise_style(enterprise_style)
        self._vnc_lib.fabric_update(fabric)

        np, rc = self.create_node_profile('node-profile-sc' + self.id(),
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {'physical_role': 'leaf',
                    'rb_roles': [role]}
                )],
            job_template=jt)

        bgp_router1, pr1 = self.create_router('device-sc' + self.id(), '3.3.3.3',
        product='qfx5110-48s-4c', family='junos-qfx',
        role='leaf', rb_roles=[role],
        physical_role=self.physical_roles['leaf'],
        overlay_role=self.overlay_roles[
                                   role], fabric=fabric,
        node_profile=np, ignore_bgp=True)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        pi_name = "xe-0/0/0"
        pi_obj_1 = PhysicalInterface(pi_name, parent_obj=pr1)
        self._vnc_lib.physical_interface_create(pi_obj_1)

        pi_name = "xe-0/0/1"
        pi_obj_2 = PhysicalInterface(pi_name, parent_obj=pr1)
        self._vnc_lib.physical_interface_create(pi_obj_2)

        if mh:
            bgp_router2, pr2 = self.create_router('device-sc-2' + self.id(), '3.3.3.2',
                                                  product='qfx5110-48s-4c', family='junos-qfx',
                                                  role='leaf', rb_roles=[role],
                                                  physical_role=self.physical_roles['leaf'],
                                                  overlay_role=self.overlay_roles[
                                                      role], fabric=fabric,
                                                  node_profile=np, ignore_bgp=True)
            pr2.set_physical_router_loopback_ip('30.30.0.1')
            self._vnc_lib.physical_router_update(pr2)

            pi_name = "xe-0/0/0"
            pi_obj_1_pr2 = PhysicalInterface(pi_name, parent_obj=pr2)
            self._vnc_lib.physical_interface_create(pi_obj_1_pr2)

        vn_obj = self.create_vn(str(3), '3.3.3.0')

        return pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, pr2, pi_obj_1_pr2


    def create_vpg_and_vmi(self, pp_obj_1, pr1, fabric, pi_obj,
                           vn_obj, pp_obj_2=None, pr2=None, pi_obj2=None, vpg_nm=1):
        device_name = pr1.get_fq_name()[-1]
        fabric_name = fabric.get_fq_name()[-1]
        phy_int_name = pi_obj.get_fq_name()[-1]

        # first create a VMI

        vpg_name = "vpg-sc-" + str(vpg_nm) + self.id()
        vlan_tag = 10

        vmi_obj_1 = VirtualMachineInterface(vpg_name + "-tagged-" + str(vlan_tag),
                                          parent_type='project',
                                          fq_name = ["default-domain", "default-project",
                                                     vpg_name + "-tagged-" + str(vlan_tag)])

        if pr2:
            # if pr2 is not none, it should also have a pi_obj2
            phy_int_name_2 = pi_obj2.get_fq_name()[-1]
            device_name_2 = pr2.get_fq_name()[-1]
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
                phy_int_name, phy_int_name, device_name, fabric_name, phy_int_name_2, phy_int_name_2,
                device_name_2, fabric_name)
        elif pi_obj2:
            phy_int_name_2 = pi_obj2.get_fq_name()[-1]
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
            phy_int_name, phy_int_name, device_name, fabric_name, phy_int_name_2, phy_int_name_2,
            device_name, fabric_name)
        else:
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (phy_int_name,
                                                                                                                                             phy_int_name,
                                                                                                                                             device_name,
                                                                                                                                             fabric_name)

        vmi_bindings = {
            "key_value_pair": [{
                "key": "vnic_type",
                "value": "baremetal"
            }, {
                "key": "vif_type",
                "value": "vrouter"
            }, {
                "key": "profile",
                "value": vmi_profile
            }]
        }


        vmi_obj_1.set_virtual_machine_interface_bindings(vmi_bindings)

        vmi_properties = {
                "sub_interface_vlan_tag": vlan_tag
            }
        vmi_obj_1.set_virtual_machine_interface_properties(vmi_properties)

        vmi_obj_1.set_virtual_network(vn_obj)


        vmi_obj_2 = VirtualMachineInterface(vpg_name + "-untagged-" + str(vlan_tag),
                                            parent_type='project',
                                            fq_name = ["default-domain", "default-project",
                                                       vpg_name + "-untagged-" + str(vlan_tag)])

        vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (phy_int_name,
                                                                                                                                             phy_int_name,
                                                                                                                                             device_name,
                                                                                                                                             fabric_name)

        vmi_bindings = {
            "key_value_pair": [{
                "key": "vnic_type",
                "value": "baremetal"
            }, {
                "key": "vif_type",
                "value": "vrouter"
            }, {
                "key": "profile",
                "value": vmi_profile
            }, {
                "key": "tor_port_vlan_id",
                "value": str(vlan_tag)
            }]
        }


        vmi_obj_2.set_virtual_machine_interface_bindings(vmi_bindings)

        vmi_obj_2.set_virtual_network(vn_obj)

        # now create a VPG
        vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric)
        if pi_obj2:
            vpg_obj.set_physical_interface_list([
                pi_obj2.get_fq_name(),
                pi_obj.get_fq_name()],
                [{'ae_num': 0},
                 {'ae_num': 0}])
        else:
            vpg_obj.set_physical_interface(pi_obj)
        self._vnc_lib.virtual_port_group_create(vpg_obj)

        vmi_obj_1.set_port_profile(pp_obj_1)
        self._vnc_lib.virtual_machine_interface_create(vmi_obj_1)

        vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()}])

        if pp_obj_2:
            vmi_obj_2.set_port_profile(pp_obj_2)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj_2)
            vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()},
                                                        {'uuid': vmi_obj_2.get_uuid()}])
        self._vnc_lib.virtual_port_group_update(vpg_obj)


        return vmi_obj_1

    def delete_objects(self):

        vpg_list = self._vnc_lib.virtual_port_groups_list().get('virtual-port-groups')
        for vpg in vpg_list:
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg['uuid'])
            vpg_obj.set_virtual_machine_interface_list([])
            vpg_obj.set_physical_interface_list([])
            self._vnc_lib.virtual_port_group_update(vpg_obj)

        vmi_list = self._vnc_lib.virtual_machine_interfaces_list().get(
            'virtual-machine-interfaces')
        for vmi in vmi_list:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi['uuid'])

        pi_list = self._vnc_lib.physical_interfaces_list().get('physical-interfaces')
        for pi in pi_list:
            self._vnc_lib.physical_interface_delete(id=pi['uuid'])

        for vpg in vpg_list:
            self._vnc_lib.virtual_port_group_delete(id=vpg['uuid'])

        pr_list = self._vnc_lib.physical_routers_list().get('physical-routers')
        for pr in pr_list:
            self._vnc_lib.physical_router_delete(id=pr['uuid'])

        vn_list = self._vnc_lib.virtual_networks_list().get('virtual-networks')
        for vn in vn_list:
            self._vnc_lib.virtual_network_delete(id=vn['uuid'])

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

        pp_list = self._vnc_lib.port_profiles_list().get('port-profiles')
        for pp in pp_list:
            self._vnc_lib.port_profile_delete(id=pp['uuid'])

        sc_list = self._vnc_lib.storm_control_profiles_list().get('storm-control-profiles')
        for sc in sc_list:
            self._vnc_lib.storm_control_profile_delete(id=sc['uuid'])

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
