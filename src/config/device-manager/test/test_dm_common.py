#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import gevent
from time import sleep
sys.path.append("../common/tests")
from testtools.matchers import Equals, Contains, Not
from test_utils import *
import test_common
import test_case
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *

#
# All Generaric utlity method shoud go here
#
class TestCommonDM(test_case.DMTestCase):

    def set_obj_param(self, obj, param, value):
        fun = getattr(obj, "set_" + param)
        fun(value)
    # end set_obj_param

    def get_obj_param(self, obj, param):
        fun = getattr(obj, "get_" + param)
        return fun()
    # end get_obj_param

    def get_bgp_groups(self, config, bgp_type=''):
        protocols = config.get_protocols()
        bgp = protocols.get_bgp()
        bgp_groups = bgp.get_group()
        grps = []
        for gp in bgp_groups or []:
            if not bgp_type or bgp_type == gp.get_type():
                grps.append(gp)
        return grps
    # end get_bgp_groups

    def set_hold_time(self, bgp_router, value):
        params = self.get_obj_param(bgp_router, 'bgp_router_parameters') or BgpRouterParams()
        self.set_obj_param(params, 'hold_time', 100)
        self.set_obj_param(bgp_router, 'bgp_router_parameters', params)
    # end set_hold_time

    def set_auth_data(self, bgp_router, token, password, auth_type):
        params = self.get_obj_param(bgp_router, 'bgp_router_parameters') or BgpRouterParams()
        key = AuthenticationKeyItem(token, password)
        self.set_obj_param(params, 'auth_data', AuthenticationData(auth_type, [key]))
        self.set_obj_param(bgp_router, 'bgp_router_parameters', params)
    # end set_auth_data

# end
