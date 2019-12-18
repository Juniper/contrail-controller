#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
import sys
import gevent
import itertools
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import DMUtils
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from .test_dm_common import *
from .test_dm_utils import FakeDeviceConnect

#
# All BGP related DM test cases should go here
#
class TestBgpDM(TestCommonDM):

    def __init__(self, *args, **kwargs):
        super(TestBgpDM, self).__init__(*args, **kwargs)

    @retries(5, hook=retry_exc_handler)
    def check_dm_bgp_hold_time_config(self, bgp_type, hold_time):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config, bgp_type)
        self.assertIn(hold_time, [gp.get_hold_time() for gp in bgp_groups or []])
        return

    # test hold time configuration
    def verify_dm_bgp_hold_time_config(self):
        bgp_router, pr = self.create_router('router' + self.id() , '1.1.1.1',
                                                         product=self.product)
        self.set_hold_time(bgp_router, 100)
        self._vnc_lib.bgp_router_update(bgp_router)
        self.check_dm_bgp_hold_time_config('internal', 100)
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    @retries(5, hook=retry_exc_handler)
    def check_dm_bgp_export_policy(self, product):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config)
        for gp in bgp_groups or []:
            if gp.get_type() == 'internal':
                if 'qfx5' not in product:
                    self.assertEqual(gp.get_export(), DMUtils.make_ibgp_export_policy_name())
                else:
                    self.assertIsNone(gp.get_export())
                return
            if gp.get_type() == 'external':
                self.assertThat(gp.get_export() != DMUtils.make_ibgp_export_policy_name())
                return
        self.assertTrue(False)
        return

    # test iBgp export policy configuration
    def verify_dm_bgp_export_policy(self):
        bgp_router, pr = self.create_router('router' + self.id() , '1.1.1.1',
                                                          product=self.product)
        self.check_dm_bgp_export_policy(self.product)
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    # Test Auth Confiuration
    @retries(5, hook=retry_exc_handler)
    def check_bgp_auth_config(self, bgp_type, key):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config, bgp_type)
        self.assertIn(key, [gp.get_authentication_key() for gp in bgp_groups or []])
        return

    @retries(5, hook=retry_exc_handler)
    def check_bgp_auth_neighbour_config(self, bgp_type, key):
        config = FakeDeviceConnect.get_xml_config()
        bgp_groups = self.get_bgp_groups(config, bgp_type)
        self.assertIn(key, [neigh.get_authentication_key() for neigh in
              itertools.chain.from_iterable([gp.get_neighbor() for gp in bgp_groups or []])])
        return

    # test bgp auth configuration
    def verify_dm_md5_auth_config(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                                          product=self.product)
        self.set_auth_data(bgp_router, 0, 'bgppswd', 'md5')
        self._vnc_lib.bgp_router_update(bgp_router)
        gevent.sleep(1)
        self.check_bgp_auth_config('internal', 'bgppswd')

        #bgp peering, auth validate
        bgp_router_peer, _ = self.create_router('router2' + self.id() , '20.2.2.2', product=self.product, ignore_pr=True)
        families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        auth = AuthenticationData('md5', [AuthenticationKeyItem(0, 'bgppswd-neigh')])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families, auth_data=auth)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_router.add_bgp_router(bgp_router_peer, BgpPeeringAttributes(session=bgp_sessions))
        self._vnc_lib.bgp_router_update(bgp_router)
        self.check_bgp_auth_config('internal', 'bgppswd')
        self.check_bgp_auth_config('external', 'bgppswd')
        self.check_bgp_auth_neighbour_config('external', 'bgppswd-neigh')
        bgp_peer_fq = bgp_router_peer.get_fq_name()
        self.delete_routers(bgp_router_peer)
        self.wait_for_routers_delete(bgp_peer_fq)
        bgp_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_fq, pr_fq)
    #end test_dm_md5_auth_config

    @retries(5, hook=retry_exc_handler)
    def check_lo0_ip_config(self, ip_check=''):
        config = FakeDeviceConnect.get_xml_config()
        intfs = self.get_interfaces(config, "lo0")
        if ip_check:
            ips = self.get_ip_list(intfs[0], "v4", "0")
            self.assertEqual(ip_check, ips[0])
        else:
            if not intfs or not self.get_ip_list(intfs[0], "v4", "0"):
                return
            self.assertTrue(False)
        return
    # end check_lo0_ip_config

    @retries(5, hook=retry_exc_handler)
    def check_tunnel_source_ip(self, ip_check='', look_for=True):
        config = FakeDeviceConnect.get_xml_config()
        tunnels = self.get_dynamic_tunnels(config) or DynamicTunnels()
        if look_for:
            self.assertIn(ip_check, [tunnel.source_address
                           for tunnel in tunnels.get_dynamic_tunnel()])
        else:
            self.assertNotIn(ip_check, [tunnel.source_address
                           for tunnel in tunnels.get_dynamic_tunnel()])
        return
    # end check_tunnel_source_ip

    # test loopback ip configuration
    def verify_dm_lo0_ip_config(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                                          product=self.product)
        self.check_lo0_ip_config()
        tunnels_needed = True
        if 'qfx5' in self.product:
            tunnels_needed = False

        pr.set_physical_router_loopback_ip("10.10.0.1")
        self._vnc_lib.physical_router_update(pr)
        self.check_lo0_ip_config("10.10.0.1/32")
        self.check_tunnel_source_ip("10.10.0.1", tunnels_needed)

        pr.set_physical_router_dataplane_ip("20.20.0.1")
        self._vnc_lib.physical_router_update(pr)
        self.check_tunnel_source_ip("20.20.0.1", tunnels_needed)
        self.check_lo0_ip_config("10.10.0.1/32")

        pr.set_physical_router_loopback_ip('')
        self._vnc_lib.physical_router_update(pr)
        self.check_lo0_ip_config()
        self.check_tunnel_source_ip("20.20.0.1", tunnels_needed)

        pr.set_physical_router_dataplane_ip('')
        self._vnc_lib.physical_router_update(pr)
        self.check_tunnel_source_ip("10.10.0.1", False)
        self.check_tunnel_source_ip("20.20.0.1", False)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)


    @retries(5, hook=retry_exc_handler)
    def check_router_id_config(self, ip_check=''):
        config = FakeDeviceConnect.get_xml_config()
        ri_opts = config.get_routing_options()
        self.assertIsNotNone(ri_opts)
        self.assertEqual(ip_check, ri_opts.get_router_id())
    # end check_router_id_config

    # test router id configuration
    def verify_dm_router_id_config(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                                          product=self.product)
        # defaults to bgp address
        self.check_router_id_config('1.1.1.1')

        params = self.get_obj_param(bgp_router, 'bgp_router_parameters') or BgpRouterParams()
        self.set_obj_param(params, 'identifier', '5.5.5.5')
        self.set_obj_param(bgp_router, 'bgp_router_parameters', params)
        self._vnc_lib.bgp_router_update(bgp_router)
        # if identifier is set, use it to conifgure router-id
        self.check_router_id_config('5.5.5.5')

        # cleanup
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)
    # end test_dm_router_id_config

# end TestBgpDM

