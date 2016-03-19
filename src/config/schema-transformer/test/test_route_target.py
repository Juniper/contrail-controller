#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

try:
    import config_db
except ImportError:
    from schema_transformer import config_db
from vnc_api.vnc_api import RouteTargetList, NoIdError

from test_case import STTestCase, retries
from test_policy import VerifyPolicy


class VerifyRouteTarget(VerifyPolicy):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def check_rt_is_deleted(self, name):
        try:
            self._vnc_lib.route_target_read(fq_name=[name])
            print "retrying ... ", test_common.lineno()
            raise Exception('rt %s still exists' % name)
        except NoIdError:
            print 'rt deleted'


class TestRouteTarget(STTestCase, VerifyRouteTarget):
    def test_configured_targets(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        self.wait_to_get_object(config_db.RoutingInstanceST,
                                vn1_obj.get_fq_name_str()+':'+vn1_name)

        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn1_obj.set_route_target_list(rtgt_list)
        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True, 'import')

        exp_rtgt_list.route_target.append('target:1:1')
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True, 'export')

        imp_rtgt_list.route_target.append('target:1:1')
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True, 'import')

        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True, 'import')

        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=vn1_obj.fq_name+[vn1_obj.name])
    # end test_configured_targets
# end class TestRouteTarget
