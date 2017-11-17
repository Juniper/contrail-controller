#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys

try:
    import config_db
except ImportError:
    from schema_transformer import config_db
from vnc_api.vnc_api import RouteTargetList, NoIdError
from vnc_cfg_api_server import db_manage

from test_case import STTestCase, retries
from test_policy import VerifyPolicy

sys.path.append("../common/tests")
import test_common

class VerifyRouteTarget(VerifyPolicy):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def check_rt_is_deleted(self, name):
        try:
            rt_obj = self._vnc_lib.route_target_read(fq_name=[name])
            print "retrying ... ", test_common.lineno()
            raise Exception(
                'rt %s still exists: RI backrefs %s LR backrefs %s' % (
                    name, rt_obj.get_routing_instance_back_refs(),
                    rt_obj.get_logical_router_back_refs()))
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

    @retries(5)
    def wait_for_route_target(self, ri_obj):
        return self._vnc_lib.route_target_read(
                ri_obj.get_route_target_refs()[0]['to'])

    def test_db_manage_zk_route_target_missing(self):
        # create  vn
        vn_name = 'vn_' + self.id()
        vn_obj = self.create_virtual_network(vn_name, '10.0.0.0/24')
        ri_obj = self._vnc_lib.routing_instance_read(
                vn_obj.get_routing_instances()[0]['to'])
        rt_obj = self.wait_for_route_target(ri_obj)
        rt_id_str = "%(#)010d" % {
                '#': int(rt_obj.get_fq_name_str().split(':')[-1])}
        db_checker = db_manage.DatabaseChecker(
            *db_manage._parse_args('check --cluster_id %s' % self._cluster_id))
        db_healer = db_manage.DatabaseHealer(
            *db_manage._parse_args('--execute heal --cluster_id %s' % self._cluster_id))
        path = '%s%s/%s' % (
                self._cluster_id, db_checker.BASE_RTGT_ID_ZK_PATH, rt_id_str)
        print "make sure node exists in zk"
        self.assertIsNotNone(db_checker._zk_client.get(path))
        print "Remove node from zk"
        with db_checker._zk_client.patch_path(path):
            print "check for node to be missing in zk"
            errors = db_checker.check_route_targets_id()
            error_types = [type(x) for x in errors]
            self.assertIn(db_manage.ZkRTgtIdMissingError, error_types)
            print "heal missing node in zk"
            db_healer.heal_route_targets_id()
            print "check for node to be re-created in zk by heal"
            errors = db_checker.check_route_targets_id()
            self.assertEqual([], errors)
    # test_db_manage_zk_route_target_missing

# end class TestRouteTarget
