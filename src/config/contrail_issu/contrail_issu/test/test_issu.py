#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
sys.path.append("../schema-transformer/schema_transformer/tests")
sys.path.append("../schema-transformer")
sys.path.append("contrail_issu")
from testtools.matchers import Equals, Contains, Not
from cfgm_common.tests.test_utils import *
from cfgm_common.tests import test_common
import test_case
from test_policy import VerifyPolicy
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import logging
from flexmock import flexmock

sys.modules['paramiko'] = flexmock()

import issu_contrail_config

import issu_contrail_pre_sync 
from issu_contrail_pre_sync import _issu_cassandra_pre_sync_main

import issu_contrail_run_sync
from issu_contrail_run_sync import ICKombuClient, _issu_rmq_main

import issu_contrail_post_sync
from issu_contrail_post_sync import _issu_cassandra_post_sync_main

from issu_contrail_zk_sync import _issu_zk_main

from vnc_api.vnc_api import *
try:
    import to_bgp
except ImportError:
    from schema_transformer import to_bgp

from gevent import sleep

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

        self.assertTill(self.ifmap_has_ident, obj=vn1_obj)
        self.assertTill(self.ifmap_has_ident, obj=vn2_obj)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn1_obj))

    # end basic_issu_policy_pre

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

        self.assertTill(self.ifmap_has_ident, obj=vn3_obj)
        self.assertTill(self.ifmap_has_ident, obj=vn4_obj)

        self.check_ri_ref_present(self.get_ri_name(vn3_obj),
                                  self.get_ri_name(vn4_obj))
        self.check_ri_ref_present(self.get_ri_name(vn4_obj),
                                  self.get_ri_name(vn3_obj))
    # end basic_issu_policy_post


    def test_issu_policy(self):

        self._api_server._db_conn._db_resync_done.wait()

        self.basic_issu_policy_pre()
        _issu_cassandra_pre_sync_main()

        extra_config_knobs=[
                ('DEFAULTS','ifmap_server_port','8448'),
                ('DEFAULTS','rabbit_vhost','/v2'),
                ('DEFAULTS','cluster_id','v2'),]
        self.new_api_server_info = test_common.create_api_server_instance(
            self.id(), extra_config_knobs)
        self.new_api_server = self.new_api_server_info['api_server']
        print "launched the 2nd api server"
        self.new_api_server._db_conn._db_resync_done.wait()

        self._issu_rmq_greenlet = gevent.spawn(_issu_rmq_main)
        threads = [self._issu_rmq_greenlet]
        gevent.joinall(threads, timeout=4)

        self.basic_issu_policy_post()
        _issu_cassandra_post_sync_main()
        _issu_zk_main()

        _graph_v1 = dict(FakeIfmapClient._graph['8443'])
        _graph_v2 = dict(FakeIfmapClient._graph['8448'])

        ifmap_diff = set(_graph_v1.keys()) - set(_graph_v2.keys())
        if not ifmap_diff:
            print "issu ut successful"

#end class TestIssu

