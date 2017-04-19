import sys
import json
import gevent
from gevent import monkey
monkey.patch_all()

import unittest
from flexmock import flexmock

from opserver.flow import FlowQuerier
from opserver.opserver_util import OpServerUtils

test_num = 0
query_dict = {}
got_expected_log_str = False
result_1 = [
{u'vmi_uuid': u'caa07fcb-ae79-441b-bc90-fa735846f137', u'sg_rule_uuid': u'18be4115-48dd-4623-8b0e-113a1a65d527', u'agg-packets': 2024, u'dport': 0, u'UuidKey': u'60487e4e-ae33-4dbd-8105-052d831f36dd', u'vrouter': u'a6s10', u'direction_ing': 1, u'sport': 17409, u'destip': u'1.1.1.4', u'nw_ace_uuid': u'00000000-0000-0000-0000-000000000001', u'agg-bytes': 198352, u'other_vrouter_ip': u'10.84.13.10', u'teardown_time': None, u'action': u'pass', u'drop_reason': u'no_route', u'protocol': 1, u'setup_time': 1442595479379251, u'sourceip': u'1.1.1.3', u'vrouter_ip': u'10.84.13.10', u'destvn': u'default-domain:admin:vn1', u'sourcevn': u'default-domain:admin:vn1'}
]

class FlowQuerierTest(unittest.TestCase):

    @staticmethod
    def custom_post_url_http(url, params):
        global query_dict
        query_dict = json.loads(params)
        return '{"href": "/analytics/query/a415fe1e-51cb-11e5-aab0-00000a540d2d"}'

    @staticmethod
    def custom_get_query_result(opserver_ip, opserver_port, qid):
        if (test_num == 1):
            return result_1
            #return []
        elif (test_num == 2):
            return result_2
        elif (test_num == 3):
            return result_3
        else:
            return []

    def custom_output(self, outputdict):
        if (test_num == 1):
            expect_result = {'flow_uuid': u'60487e4e-ae33-4dbd-8105-052d831f36dd', 'destination_vn': u'default-domain:admin:vn1', 'direction': 'ingress', 'teardown_ts': 'Active', 'protocol': 'ICMP', 'other_vrouter_ip': u' [DST-VR:10.84.13.10]', 'agg_bytes': 198352, 'source_ip': u'1.1.1.3', 'destination_ip': u'1.1.1.4', 'setup_ts': '2015 Sep 18 09:57:59.379251', 'source_port': 17409, 'nw_ace_uuid': u'00000000-0000-0000-0000-000000000001', 'tunnel_info': '', 'agg_pkts': 2024, 'vrouter': u'a6s10', 'src_vmi_uuid': u' [SRC VMI UUID:caa07fcb-ae79-441b-bc90-fa735846f137]', 'action': u'pass', 'drop_reason': u'no_route', 'destination_port': 0, 'source_vn': u'default-domain:admin:vn1', 'vrouter_ip': u'/10.84.13.10', 'sg_rule_uuid': u'18be4115-48dd-4623-8b0e-113a1a65d527'}
            for key in expect_result:
                self.assertTrue(outputdict[key] == expect_result[key])

    def setUp(self):
        self._querier = FlowQuerier()

        flexmock(OpServerUtils).should_receive('post_url_http').once().replace_with(lambda x, y, w, z: self.custom_post_url_http(x, y))
        flexmock(OpServerUtils).should_receive('get_query_result').once().replace_with(lambda x, y, z, a, b: self.custom_get_query_result(x, y, z))
        flexmock(self._querier).should_receive('output').replace_with(lambda x: self.custom_output(x))

    #@unittest.skip("skip test_1_noarg_query")
    def test_1_noarg_query(self):
        global test_num
        global query_dict
        test_num = 1

        argv = sys.argv
        sys.argv = "contrail-flows".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"table": "FlowRecordTable", "dir": 1, "select_fields": ["UuidKey", "vrouter", "setup_time", "teardown_time", "sourcevn", "destvn", "sourceip", "destip", "protocol", "sport", "dport", "action", "direction_ing", "agg-bytes", "agg-packets", "sg_rule_uuid", "nw_ace_uuid", "vrouter_ip", "other_vrouter_ip", "vmi_uuid", "drop_reason"]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_dict['end_time']) - int(query_dict['start_time']), 10*60*pow(10,6))
        del query_dict['start_time']
        del query_dict['end_time']
        for key in expected_result_dict:
            self.assertTrue(key in query_dict)
            self.assertTrue(expected_result_dict[key] == query_dict[key])

if __name__ == '__main__':
    unittest.main()
