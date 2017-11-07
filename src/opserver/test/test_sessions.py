#!/usr/bin/env python

#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

#
# test_sessions.py
#
# Unit tests for contrail-sessions script
#
import sys
import json
import gevent
from gevent import monkey
monkey.patch_all()

import unittest
from flexmock import flexmock

from opserver.sessions import SessionQuerier
from opserver.opserver_util import OpServerUtils

test_num = 0
query_dict = {}
got_expected_log_str = False
results = [
'{"value": [\n{"T=":1442446560000000,"SUM(forward_sampled_bytes)":0}, {"T=":1442446620000000,"SUM(forward_sampled_bytes)":416222}, {"T=":1442446680000000,"SUM(forward_sampled_bytes)":410806}, {"T=":1442446740000000,"SUM(forward_sampled_bytes)":171222}, {"T=":1442446800000000,"SUM(forward_sampled_bytes)":168722}, {"T=":1442446860000000,"SUM(forward_sampled_bytes)":164973}, {"T=":1442446920000000,"SUM(forward_sampled_bytes)":167889}, {"T=":1442446980000000,"SUM(forward_sampled_bytes)":165}, {"T=":1442447040000000,"SUM(forward_sampled_bytes)":166639}, {"T=":1442447100000000,"SUM(forward_sampled_bytes)":170805}, {"T=":1442447160000000,"SUM(forward_sampled_bytes)":165806}\n]}',
'{"value": [\n{"forward_flow_uuid":"e541b3bd-a804-4b94-afc2-e06c9c02b05e", "reverse_flow_uuid":"e541b3bd-a804-4b94-afc2-e06c9c02b05e", "local_ip":"1.0.0.2", "server_port":8080}\n]}'
]
class StatQuerierTest(unittest.TestCase):

    @staticmethod
    def custom_post_url_http(url, params, sync = True):
        global query_dict
        query_dict = json.loads(params)
        return results[test_num]

    def custom_display(self, result):
        expect_result = json.loads(results[test_num])
        expect_result = expect_result['value']
        self.assertTrue(result == expect_result)

    def setUp(self):
        self._querier = SessionQuerier()

        flexmock(OpServerUtils).should_receive('post_url_http').once().replace_with(lambda x, y, w, z, **kwargs: self.custom_post_url_http(x, y, kwargs))
        flexmock(self._querier).should_receive('display').replace_with(lambda x: self.custom_display(x))

    #@unittest.skip("skip test_0_session_series_query")
    def test_0_session_series_query(self):
        global test_num
        global query_dict
        test_num = 0

        argv = sys.argv
        sys.argv = "contrail-sessions --table SessionSeriesTable --select T=60 SUM(forward_sampled_bytes) --session-type client --where local_ip=1.0.0.2".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"select_fields": ["T=60", "SUM(forward_sampled_bytes)"], "table": "SessionSeriesTable", "where": [[{"suffix": null, "value2": null, "name": "local_ip", "value": "1.0.0.2", "op": 1}]]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_dict['end_time']) - int(query_dict['start_time']), 10*60*pow(10,6))
        del query_dict['start_time']
        del query_dict['end_time']
        for key in expected_result_dict:
            self.assertTrue(key in query_dict)
            self.assertTrue(expected_result_dict[key] == query_dict[key])

    #@unittest.skip("skip test_1_session_record_query")
    def test_1_session_record_query(self):
        global test_num
        global query_dict
        test_num = 1

        argv = sys.argv
        sys.argv = "contrail-sessions --table SessionRecordTable --select server_port local_ip --session-type client --where server_port=8080 protocol=6".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"select_fields": ["server_port", "local_ip"], "table": "SessionRecordTable", "where": [[{"suffix": null, "value2": null, "name": "server_port", "value": 8080, "op": 1}, {"suffix": null, "value2": null, "name": "protocol", "value": 6, "op": 1}]]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_dict['end_time']) - int(query_dict['start_time']), 10*60*pow(10,6))
        del query_dict['start_time']
        del query_dict['end_time']
        for key in expected_result_dict:
            self.assertTrue(key in query_dict)
            self.assertTrue(expected_result_dict[key] == query_dict[key])

if __name__ == '__main__':
    unittest.main()
