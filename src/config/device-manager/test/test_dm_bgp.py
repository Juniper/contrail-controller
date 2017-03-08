#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import gevent
from time import sleep
sys.path.append("../common/tests")
from test_utils import *
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import DMUtils
from test_common import *
from test_dm_common import *

#
# All BGP related DM test cases should go here
#
class TestBgpDM(TestCommonDM):

    @retries(5, hook=retry_exc_handler)
    def check_dm_bgp_hold_time_config(self, bgp_type, hold_time):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config, bgp_type)
        for gp in bgp_groups or []:
            if gp.get_hold_time() == hold_time:
                self.assertTrue(True)
                return
        self.assertTrue(False)
        return

    # test hold time configuration
    def test_dm_bgp_hold_time_config(self):
        bgp_router, pr = self.create_router('router' + self.id() , '1.1.1.1')
        self.set_hold_time(bgp_router, 100)
        self._vnc_lib.bgp_router_update(bgp_router)
        gevent.sleep(2)
        self.check_dm_bgp_hold_time_config('internal', 100)

    @retries(5, hook=retry_exc_handler)
    def check_dm_bgp_export_policy(self):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config)
        for gp in bgp_groups or []:
            if gp.get_type() == 'internal':
                self.assertEqual(gp.get_export(), DMUtils.make_ibgp_export_policy_name())
                return
            if gp.get_type() == 'external':
                self.assertThat(gp.get_export() != DMUtils.make_ibgp_export_policy_name())
                return
        self.assertTrue(False)
        return

    # test iBgp export policy configuration
    def test_dm_bgp_export_policy(self):
        bgp_router, pr = self.create_router('router' + self.id() , '1.1.1.1')
        self.check_dm_bgp_export_policy()

    # Test Auth Confiuration
    @retries(5, hook=retry_exc_handler)
    def check_bgp_auth_config(self, bgp_type, key):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config, bgp_type)
        for gp in bgp_groups or []:
            if gp.get_authentication_key() == key:
                self.assertTrue(True)
                return
        self.assertTrue(False)
        return

    @retries(5, hook=retry_exc_handler)
    def check_bgp_auth_neighbour_config(self, bgp_type, key):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config, bgp_type)
        for gp in bgp_groups or []:
            neighbours = gp.get_neighbor() or []
            for neigh in neighbours:
                if neigh.get_authentication_key() == key:
                    self.assertTrue(True)
                    return
        self.assertTrue(False)
        return

    # test bgp auth configuration
    def test_dm_md5_auth_config(self):
        bgp_router, _ = self.create_router('router1' + self.id(), '1.1.1.1')
        self.set_auth_data(bgp_router, 0, 'bgppswd', 'md5')
        self._vnc_lib.bgp_router_update(bgp_router)
        gevent.sleep(2)
        self.check_bgp_auth_config('internal', 'bgppswd')

        #bgp peering, auth validate
        bgp_router_peer, _ = self.create_router('router2' + self.id() , '20.2.2.2')
        families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        auth = AuthenticationData('md5', [AuthenticationKeyItem(0, 'bgppswd-neigh')])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families, auth_data=auth)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_router.add_bgp_router(bgp_router_peer, BgpPeeringAttributes(session=bgp_sessions))
        self._vnc_lib.bgp_router_update(bgp_router)
        gevent.sleep(2)
        self.check_bgp_auth_config('internal', 'bgppswd')
        self.check_bgp_auth_config('external', 'bgppswd')
        self.check_bgp_auth_neighbour_config('internal', 'bgppswd-neigh')
    #end test_dm_md5_auth_config

# end TestBgpDM

