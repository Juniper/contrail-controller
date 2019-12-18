#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
import mock
from unittest import skip
from .test_dm_bgp import TestBgpDM

#
# All BGP related DM test cases should go here
#
class TestQfxBgpDM(TestBgpDM):

    def __init__(self, *args, **kwargs):
        self.product = "qfx5110"
        super(TestQfxBgpDM, self).__init__(*args, **kwargs)

    def setUp(self, extra_config_knobs=None):
        super(TestQfxBgpDM, self).setUp(extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestQfxBgpDM, self).tearDown()

    # test hold time configuration
    @skip("Timing failures")
    def test_dm_bgp_hold_time_config(self):
        super(TestQfxBgpDM, self).verify_dm_bgp_hold_time_config()

    # test iBgp export policy configuration
    @skip("Timing failures")
    def test_dm_bgp_export_policy(self):
        super(TestQfxBgpDM, self).verify_dm_bgp_export_policy()
    # test bgp auth configuration
    @skip("Timing failures")
    def test_dm_md5_auth_config(self):
        super(TestQfxBgpDM, self).verify_dm_md5_auth_config()

    # test loopback ip configuration
    @skip("Timing failures")
    def test_dm_lo0_ip_config(self):
        super(TestQfxBgpDM, self).verify_dm_lo0_ip_config()

    # test router id configuration
    @skip("Timing failures")
    def test_dm_router_id_config(self):
        super(TestQfxBgpDM, self).verify_dm_router_id_config()

# end TestQfxBgpDM

