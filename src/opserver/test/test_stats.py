import sys
import json
import gevent
from gevent import monkey
monkey.patch_all()

import unittest
from flexmock import flexmock

from opserver.stats import StatQuerier
from opserver.opserver_util import OpServerUtils

test_num = 0
query_dict = {}
got_expected_log_str = False
result_1 =\
'{"value": [\n{"T=":1442446560000000,"SUM(cpu_info.cpu_share)":0}, {"T=":1442446620000000,"SUM(cpu_info.cpu_share)":4.16222}, {"T=":1442446680000000,"SUM(cpu_info.cpu_share)":4.10806}, {"T=":1442446740000000,"SUM(cpu_info.cpu_share)":1.71222}, {"T=":1442446800000000,"SUM(cpu_info.cpu_share)":1.68722}, {"T=":1442446860000000,"SUM(cpu_info.cpu_share)":1.64973}, {"T=":1442446920000000,"SUM(cpu_info.cpu_share)":1.67889}, {"T=":1442446980000000,"SUM(cpu_info.cpu_share)":1.65}, {"T=":1442447040000000,"SUM(cpu_info.cpu_share)":1.66639}, {"T=":1442447100000000,"SUM(cpu_info.cpu_share)":1.70805}, {"T=":1442447160000000,"SUM(cpu_info.cpu_share)":1.65806}\n]}'

class StatQuerierTest(unittest.TestCase):

    @staticmethod
    def custom_post_url_http(url, params, sync = True):
        global query_dict
        query_dict = json.loads(params)
        if (test_num == 1):
            return result_1

        return []

    def custom_display(self, result):
        if (test_num == 1):
            expect_result = json.loads(result_1)
            expect_result = expect_result['value']
            self.assertTrue(result == expect_result)

    def setUp(self):
        self._querier = StatQuerier()

        flexmock(OpServerUtils).should_receive('post_url_http').once().replace_with(lambda x, y, w, z, **kwargs: self.custom_post_url_http(x, y, kwargs))
        flexmock(self._querier).should_receive('display').replace_with(lambda x: self.custom_display(x))


    #@unittest.skip("skip test_1_analytics_cpu_query")
    def test_1_analytics_cpu_query(self):
        global test_num
        global query_dict
        test_num = 1

        argv = sys.argv
        sys.argv = "contrail-stats --table NodeStatus.process_mem_cpu_usage --select T=60 SUM(process_mem_cpu_usage.cpu_share) --where name=*".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"select_fields": ["T=60", "SUM(process_mem_cpu_usage.cpu_share)"], "table": "StatTable.NodeStatus.process_mem_cpu_usage", "where": [[{"suffix": null, "value2": null, "name": "name", "value": "", "op": 7}]]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_dict['end_time']) - int(query_dict['start_time']), 10*60*pow(10,6))
        del query_dict['start_time']
        del query_dict['end_time']
        for key in expected_result_dict:
            self.assertTrue(key in query_dict)
            self.assertTrue(expected_result_dict[key] == query_dict[key])

if __name__ == '__main__':
    unittest.main()
