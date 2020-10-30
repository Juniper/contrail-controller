#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function

from cfgm_common.tests import test_common
import gevent
from vnc_api.vnc_api import GlobalSystemConfig, NoIdError, RouteTargetList
from vnc_cfg_api_server import db_manage

from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.resources.routing_instance import RoutingInstanceST
from schema_transformer.resources.virtual_network import VirtualNetworkST
from schema_transformer.db import SchemaTransformerDB
from schema_transformer.resources.routing_instance import RoutingInstanceST
from .test_case import retries, STTestCase
from .test_policy import VerifyPolicy


class VerifyRouteTarget(VerifyPolicy):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def check_rt_is_deleted(self, name):
        try:
            rt_obj = self._vnc_lib.route_target_read(fq_name=[name])
            print("retrying ... ", test_common.lineno())
            raise Exception(
                'rt %s still exists: RI backrefs %s LR backrefs %s' % (
                    name, rt_obj.get_routing_instance_back_refs(),
                    rt_obj.get_logical_router_back_refs()))
        except NoIdError:
            print('rt deleted')


class TestRouteTarget(STTestCase, VerifyRouteTarget):
    def test_configured_targets(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        self.wait_to_get_object(RoutingInstanceST,
                                vn1_obj.get_fq_name_str() + ':' + vn1_name)

        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn1_obj.set_route_target_list(rtgt_list)
        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True,
                            'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True,
                            'import')

        exp_rtgt_list.route_target.append('target:1:1')
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True,
                            'export')

        imp_rtgt_list.route_target.append('target:1:1')
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True,
                            'import')

        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True,
                            'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True,
                            'import')

        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=vn1_obj.fq_name + [vn1_obj.name])
    # end test_configured_targets

    @retries(5)
    def wait_for_route_target(self, vn_obj):
        ri_obj = self._vnc_lib.routing_instance_read(
            vn_obj.get_routing_instances()[0]['to'])
        return self._vnc_lib.route_target_read(
            ri_obj.get_route_target_refs()[0]['to'])

    def test_asn_change_when_zk_rt_missing(self):
        vn_obj = self.create_virtual_network('vn_for_asn' + self.id(),
                                             '10.0.0.0/24')
        ri_fq_name = vn_obj.fq_name + [vn_obj.fq_name[-1]]
        rt_obj = self.wait_for_route_target(vn_obj)
        rt_id_str = "%(#)010d" % {
            '#': int(rt_obj.get_fq_name_str().split(':')[-1])}
        db_checker = db_manage.DatabaseChecker(
            *db_manage._parse_args('check --cluster_id %s' % self._cluster_id))
        path = '%s%s%s' % (
            self._cluster_id, db_checker.BASE_RTGT_ID_ZK_PATH, rt_id_str)
        self.assertEqual(db_checker._zk_client.get(path)[0],
                         ':'.join(ri_fq_name))
        with db_checker._zk_client.patch_path(path):
            errors = db_checker.check_route_targets_id()
            error_types = [type(x) for x in errors]
            self.assertIn(db_manage.SchemaRTgtIdExtraError, error_types)
            self.assertIn(db_manage.ConfigRTgtIdExtraError, error_types)

            free_rt_orig = SchemaTransformerDB.free_route_target

            # Flag to be used to check free_route_target function failure
            has_test_failed = [False]

            def mock_free_rt(*args, **kwargs):
                try:
                    return free_rt_orig(*args, **kwargs)
                except Exception:
                    has_test_failed[0] = True

            SchemaTransformerDB.free_route_target = mock_free_rt
            gsc = self._vnc_lib.global_system_config_read(
                GlobalSystemConfig().fq_name)

            gsc.enable_4byte_as = True
            gsc.autonomous_system = 81000
            self._vnc_lib.global_system_config_update(gsc)

            # Wait for Schema_tranformer to process ASN change
            gevent.sleep(10)
            SchemaTransformerDB.free_route_target = free_rt_orig
            if has_test_failed[0]:
                self.fail("free_route_target failed in schema transformer")

    # test_db_manage_zk_route_target_missing

    def test_db_manage_zk_route_target_missing(self):
        vn_obj = self.create_virtual_network('vn_' + self.id(), '10.0.0.0/24')
        ri_fq_name = vn_obj.fq_name + [vn_obj.fq_name[-1]]
        rt_obj = self.wait_for_route_target(vn_obj)
        rt_id_str = "%(#)010d" % {
            '#': int(rt_obj.get_fq_name_str().split(':')[-1])}
        db_checker = db_manage.DatabaseChecker(
            *db_manage._parse_args('check --cluster_id %s' % self._cluster_id))
        db_cleaner = db_manage.DatabaseCleaner(
            *db_manage._parse_args('--execute clean --cluster_id %s' %
                                   self._cluster_id))
        path = '%s%s%s' % (
            self._cluster_id, db_checker.BASE_RTGT_ID_ZK_PATH, rt_id_str)
        self.assertEqual(db_checker._zk_client.get(path)[0],
                         ':'.join(ri_fq_name))
        with db_checker._zk_client.patch_path(path):
            errors = db_checker.check_route_targets_id()
            error_types = [type(x) for x in errors]
            self.assertIn(db_manage.SchemaRTgtIdExtraError, error_types)
            self.assertIn(db_manage.ConfigRTgtIdExtraError, error_types)

            db_cleaner.clean_stale_route_target_id()
            errors = db_checker.check_route_targets_id()
            self.assertEqual([], errors)
            self.assertIsNone(db_checker._zk_client.exists(path))
            self.assertRaises(NoIdError, self._vnc_lib.route_target_read,
                              id=rt_obj.uuid)

            test_common.reinit_schema_transformer()
            new_rt_obj = self.wait_for_route_target(vn_obj)
            new_rt_id_str = "%(#)010d" % {
                '#': int(new_rt_obj.get_fq_name_str().split(':')[-1])}
            new_path = '%s%s%s' % (
                self._cluster_id,
                db_checker.BASE_RTGT_ID_ZK_PATH,
                new_rt_id_str,
            )
            self.assertEqual(db_checker._zk_client.get(new_path)[0],
                             ':'.join(ri_fq_name))
    # end test_db_manage_zk_route_target_missing

    def test_route_target_of_virtual_network_deleted(self):
        vn = self.create_virtual_network('vn-%s' % self.id(), '10.0.0.0/24')
        ri_fq_name = vn.fq_name + [vn.fq_name[-1]]
        self.wait_to_get_object(RoutingInstanceST,
                                ':'.join(ri_fq_name))
        ri = self._vnc_lib.routing_instance_read(ri_fq_name)
        rt = self.wait_for_route_target(vn)

        dbe_delete_orig = self._api_server._db_conn.dbe_delete

        def mock_dbe_delete(*args, **kwargs):
            if (args[0] == 'routing_instance' and
                    args[1] == ri.uuid):
                gevent.sleep(3)
            return dbe_delete_orig(*args, **kwargs)
        self._api_server._db_conn.dbe_delete = mock_dbe_delete

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self.check_rt_is_deleted(rt.fq_name[0])

    def test_ensure_missing_ri_rt_ref_recreated_on_next_vn_evaluate(self):
        """ Validate CEM-19894
        """
        # Mock the resource update to raise exception during RI locatr
        resource_update_orig = ResourceBaseST.resource_update
        def mock_ri_update(*args, **kwargs):
            if args[1] == 'routing_instance':
                raise Exception("Mocked RI update failure exception")
            return resource_update_orig(*args[1:], **kwargs)
        ResourceBaseST.resource_update = mock_ri_update

        vn1_name = self.id() + 'vn1'
        # Mock the VN evauate to wait until the simulated locate
        # failure(exception causing the RI-->RT ref to be not added) is ensured
        self.blocked = True
        self.second_evaluate = False
        ri_evaluate_orig = VirtualNetworkST.evaluate
        def mock_ri_evaluate(*args, **kwargs):
            if args[0].obj.name == vn1_name:
                while self.second_evaluate and self.blocked:
                    gevent.sleep(1)
                self.second_evaluate = True
            return ri_evaluate_orig(*args, **kwargs)
        VirtualNetworkST.evaluate = mock_ri_evaluate

        # create  vn1
        vn1_obj = self.create_virtual_network(vn1_name, rt_list=['target:1:1'])
        self.wait_to_get_object(RoutingInstanceST,
                                vn1_obj.get_fq_name_str() + ':' + vn1_name)

        # ensure RI to RT ref is removed
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', False)
        self.blocked = False

        # revert the mocks
        ResourceBaseST.resource_update = resource_update_orig
        VirtualNetworkST.evaluate = mock_ri_evaluate

        # ensure RI to RT ref is recreated as part of VN evaluate(caused by any other change in VN)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
    # end test_missing_ri_to_rt_ref_created_during_vn_update

# end class TestRouteTarget
