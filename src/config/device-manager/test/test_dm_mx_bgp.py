#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from test_dm_bgp import TestBgpDM

#
# All BGP related DM test cases should go here
#
class TestMxBgpDM(TestBgpDM):

    def __init__(self, *args, **kwargs):
        self.product = "mx"
        super(TestMxBgpDM, self).__init__(*args, **kwargs)

    # test hold time configuration
    def test_dm_bgp_hold_time_config(self):
        super(TestMxBgpDM, self).verify_dm_bgp_hold_time_config()

    # test iBgp export policy configuration
    def test_dm_bgp_export_policy(self):
        super(TestMxBgpDM, self).verify_dm_bgp_export_policy()
    # test bgp auth configuration
    def test_dm_md5_auth_config(self):
        super(TestMxBgpDM, self).verify_dm_md5_auth_config()

    # test loopback ip configuration
    def test_dm_lo0_ip_config(self):
        super(TestMxBgpDM, self).verify_dm_lo0_ip_config()

    # test router id configuration
    def test_dm_router_id_config(self):
        super(TestMxBgpDM, self).verify_dm_router_id_config()

# end TestMxBgpDM
