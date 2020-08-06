#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
import sys
import gevent
import logging
from flexmock import flexmock
import unittest
from cfgm_common.tests import test_common
from schema_transformer.tests import test_case
from schema_transformer.tests.test_policy import VerifyPolicy
from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import VirtualNetworkPolicyType, SequenceType

sys.modules['paramiko'] = flexmock()

from contrail_issu import issu_contrail_config
from contrail_issu.issu_contrail_pre_sync import _issu_cassandra_pre_sync_main
from contrail_issu.issu_contrail_run_sync import ICKombuClient, _issu_rmq_main
from contrail_issu.issu_contrail_post_sync import _issu_cassandra_post_sync_main
from contrail_issu.issu_contrail_zk_sync import _issu_zk_main


flexmock(ICKombuClient)
ICKombuClient.should_receive('_reinit_control')

flexmock(logging)
logging.should_receive('basicConfig')

flexmock(issu_contrail_config)
issu_contrail_config.should_receive('issu_info_pre').and_return([
    (None, 'config_db_uuid', {
        'obj_uuid_table': {},
        'obj_fq_name_table': {},
        'obj_shared_table': {}}),
    (None, 'to_bgp_keyspace', {
        'route_target_table': {}, 'service_chain_table': {},
        'service_chain_ip_address_table': {},
        'service_chain_uuid_table': {}}),
    (None, 'useragent', {'useragent_keyval_table': {}}),
    (None, 'svc_monitor_keyspace', {
        'pool_table': {}, 'service_instance_table': {}})])
issu_contrail_config.should_receive('issu_info_post').and_return([
    (None, 'to_bgp_keyspace', {
        'route_target_table': {}, 'service_chain_table': {},
        'service_chain_ip_address_table': {},
        'service_chain_uuid_table': {}}),
    (None, 'useragent', {'useragent_keyval_table': {}}),
    (None, 'svc_monitor_keyspace', {
        'pool_table': {}, 'service_instance_table': {}})])
issu_contrail_config.should_receive('issu_keyspace_dm_keyspace').and_return({})


class TestIssu(test_case.STTestCase, VerifyPolicy):

    def basic_issu_policy_pre(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np = self.create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_create(vn1_obj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        self.assertTill(self.vnc_db_has_ident, obj=vn1_obj)
        self.assertTill(self.vnc_db_has_ident, obj=vn2_obj)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn1_obj))

    def basic_issu_policy_post(self):
        vn3_name = self.id() + 'vn3'
        vn4_name = self.id() + 'vn4'
        vn3_obj = VirtualNetwork(vn3_name)
        vn4_obj = VirtualNetwork(vn4_name)

        np = self.create_network_policy(vn3_obj, vn4_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn3_obj.set_network_policy(np, vnp)
        vn4_obj.set_network_policy(np, vnp)

        self._vnc_lib.virtual_network_create(vn3_obj)
        self._vnc_lib.virtual_network_create(vn4_obj)

        self.assertTill(self.vnc_db_has_ident, obj=vn3_obj)
        self.assertTill(self.vnc_db_has_ident, obj=vn4_obj)

        self.check_ri_ref_present(self.get_ri_name(vn3_obj),
                                  self.get_ri_name(vn4_obj))
        self.check_ri_ref_present(self.get_ri_name(vn4_obj),
                                  self.get_ri_name(vn3_obj))

    # to let tox be successful while no other tests exist
    def test_fake(self):
        pass

    @unittest.skip("need refactor")
    def test_issu_policy(self):
        self._api_server._db_conn._db_resync_done.wait()
        self.basic_issu_policy_pre()
        _issu_cassandra_pre_sync_main()

        extra_config_knobs = [
            ('DEFAULTS', 'ifmap_server_port', '8448'),
            ('DEFAULTS', 'rabbit_vhost', '/v2'),
            ('DEFAULTS', 'cluster_id', 'v2')]
        self.new_api_server_info = test_common.create_api_server_instance(
            self.id(), extra_config_knobs)
        self.new_api_server = self.new_api_server_info['api_server']
        print("launched the 2nd api server")
        self.new_api_server._db_conn._db_resync_done.wait()

        self._issu_rmq_greenlet = gevent.spawn(_issu_rmq_main)
        threads = [self._issu_rmq_greenlet]
        gevent.joinall(threads, timeout=4)

        self.basic_issu_policy_post()
        _issu_cassandra_post_sync_main()
        _issu_zk_main()
