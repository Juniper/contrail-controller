#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import json
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestAnsibleStormControlDM(TestAnsibleCommonDM):

    def test_storm_control_profile_update(self):
        # create objects

        sc_name = 'strm_ctrl_upd'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_upd')

        #set one storm control profile to the port profile object
        pp_obj.set_storm_control_profile(sc_obj)

        self.create_vpg_and_vmi(pp_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        self.assertEqual(storm_control_profile.get('name'), sc_name)
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), '')

        # now update the storm control profile object and check if
        # update causes device abstract config also to update

        sc_obj.set_storm_control_bandwidth(40)
        sc_obj.set_recovery_timeout(1200)

        self._vnc_lib.storm_control_profile_update(sc_obj)

        # Now check the changes in the device abstract config
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profile = device_abstract_config.get('storm_control')
        self.assertEqual(storm_control_profile.get('name'), sc_name)
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), 40)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), 1200)

        self._vnc_lib.port_profile_delete(fq_name=pp_obj.get_fq_name())
        self._vnc_lib.storm_control_profile_delete(sc_obj.get_fq_name())

    def test_port_profile_vmi_association(self):
        # create objects

        sc_name = 'strm_ctrl_vmi'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_vmi')

        #set one storm control profile to the port profile object
        pp_obj.set_storm_control_profile(sc_obj)

        self.create_vpg_and_vmi(pp_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        self.assertEqual(storm_control_profile.get('name'), sc_name)
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), 600)

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/0" in log_intf.get('name'):
                    self.assertEqual(log_intf.get('storm_control_profile'), sc_name)

        self._vnc_lib.port_profile_delete(fq_name=pp_obj.get_fq_name())
        self._vnc_lib.storm_control_profile_delete(fq_name=sc_obj.get_fq_name())

    def test_disassociate_PP_from_VMI(self):
        sc_name = 'strm_ctrl_pp'
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_obj = self.create_storm_control_profile(sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_obj = self.create_port_profile('port_profile_pp')

        #set one storm control profile to the port profile object
        pp_obj.set_storm_control_profile(sc_obj)

        vmi_obj = self.create_vpg_and_vmi(pp_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get(
            'features', {}).get('storm-control',{}).get('storm_control', [])
        storm_control_profile = storm_control_profiles[-1]
        self.assertEqual(storm_control_profile.get('name'), sc_name)
        self.assertEqual(storm_control_profile.get('bandwidth_percent'), bw_percent)
        self.assertEqual(storm_control_profile.get('actions'), actions)
        self.assertEqual(storm_control_profile.get('traffic_type'), traffic_type)
        self.assertEqual(storm_control_profile.get('recovery_timeout'), 600)

        # now disassociate the port profile from VMI

        vmi_obj.set_port_profile_list([])
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        storm_control_profiles = device_abstract_config.get('storm_control')
        self.assertIsNone(storm_control_profiles)

        phy_interfaces = device_abstract_config.get(
            'features', {}).get('storm-control', {}).get('physical_interfaces', [])
        for phy_int in phy_interfaces:
            log_intfs = phy_int.get('logical_interfaces', [])
            for log_intf in log_intfs:
                if "xe-0/0/0" in log_intf.get('name'):
                    self.assertIsNone(log_intf.get('storm_control_profile'))


        self._vnc_lib.port_profile_delete(fq_name=pp_obj.get_fq_name())
        self._vnc_lib.storm_control_profile_delete(fq_name=sc_obj.get_fq_name())

    def create_vpg_and_vmi(self, pp_obj):

        jt = self.create_job_template('job-template-sc' + self.id())
        fabric = self.create_fabric('fab-sc' + self.id())
        np, rc = self.create_node_profile('node-profile-sc' + self.id(),
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {'physical_role': 'leaf',
                    'rb_roles': ['crb-access']}
                )],
            job_template=jt)

        bgp_router1, pr1 = self.create_router('device-sc' + self.id(), '3.3.3.3',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['crb-access'], fabric=fabric,
            node_profile=np, ignore_bgp=True)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        pi_name = "xe-0/0/0"
        pi_obj = PhysicalInterface(pi_name, parent_obj=pr1)
        self._vnc_lib.physical_interface_create(pi_obj)

        device_name = pr1.get_fq_name()[-1]
        fabric_name = fabric.get_fq_name()[-1]

        # first create a VMI

        vpg_name = "vpg-sc" + self.id()
        vlan_tag = 10

        vmi_obj = VirtualMachineInterface(vpg_name + "-" + str(vlan_tag),
                                          parent_type='project',
                                          fq_name = ["default-domain", "default-project",
                                                     vpg_name + "-" + str(vlan_tag)])

        vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % ('xe-0/0/0', device_name, fabric_name)

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


        vmi_obj.set_virtual_machine_interface_bindings(vmi_bindings)

        vmi_properties = {
                "sub_interface_vlan_tag": vlan_tag
            }
        vmi_obj.set_virtual_machine_interface_properties(vmi_properties)
        vmi_obj.set_port_profile(pp_obj)

        vn_obj = VirtualNetwork("vn-sc" + self.id())

        self._vnc_lib.virtual_network_create(vn_obj)
        vmi_obj.set_virtual_network(vn_obj)

        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # now create a VPG
        vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric)
        vpg_obj.set_physical_interface(pi_obj)
        vpg_obj.set_virtual_machine_interface(vmi_obj)
        self._vnc_lib.virtual_port_group_create(vpg_obj)

        return vmi_obj

