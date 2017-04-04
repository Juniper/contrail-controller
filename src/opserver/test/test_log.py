import sys
import json
import gevent
from gevent import monkey
monkey.patch_all()

import unittest
from flexmock import flexmock

from opserver.log import LogQuerier
from opserver.opserver_util import OpServerUtils
from multiprocessing import Process

test_num = 0
query_list = []
query_result = {
1: [
{u'Category': None, u'NodeType': u'Config', u'Level': 2147483647, u'InstanceId': u'0', u'Messagetype': u'UveVirtualNetworkConfigTrace', u'Source': u'a6s45', u'SequenceNum': 6867683, u'MessageTS': 1442429588898861, u'Xmlmessage': u'<UveVirtualNetworkConfigTrace type="sandesh"><data type="struct" identifier="1"><UveVirtualNetworkConfig><name type="string" identifier="1" key="ObjectVNTable">default-domain:demo:svc-vn-left</name><connected_networks type="list" identifier="4" aggtype="union"><list type="string" size="0" /></connected_networks><partially_connected_networks type="list" identifier="5" aggtype="union"><list type="string" size="0" /></partially_connected_networks><routing_instance_list type="list" identifier="6" aggtype="union"><list type="string" size="1"><element>svc-vn-left</element></list></routing_instance_list><total_acl_rules type="i32" identifier="7">0</total_acl_rules></UveVirtualNetworkConfig></data></UveVirtualNetworkConfigTrace>', u'Type': 6, u'ModuleId': u'contrail-schema'}
],
2: [
{u'Category': None, u'NodeType': u'Analytics', u'Level': 2147483647, u'InstanceId': u'0', u'Messagetype': u'GeneratorDbStatsUve', u'Source': u'a6s45', u'SequenceNum': 56411, u'MessageTS': 1442429889555171, u'Xmlmessage': u'<GeneratorDbStatsUve type="sandesh"><data type="struct" identifier="1"><GeneratorDbStats><name type="string" identifier="1" key="ObjectGeneratorInfo">a6s10:Compute:contrail-vrouter-agent:0</name><table_info type="list" identifier="3" tags=".table_name"><list type="struct" size="12"><DbTableInfo><table_name type="string" identifier="1">MessageTable</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">MessageTableCategory</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">MessageTableKeyword</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">0</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">MessageTableMessageType</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">MessageTableModuleId</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">MessageTableSource</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">MessageTableTimestamp</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">ObjectTable</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">ObjectValueTable</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">12</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">StatsTableByDblTagV3</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">1</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">StatsTableByStrTagV3</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">102</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">StatsTableByU64TagV3</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">2</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo></list></table_info><errors type="list" identifier="4" tags=""><list type="struct" size="1"><DbErrors><write_tablespace_fails type="u64" identifier="1">0</write_tablespace_fails><read_tablespace_fails type="u64" identifier="2">0</read_tablespace_fails><write_table_fails type="u64" identifier="3">0</write_table_fails><read_table_fails type="u64" identifier="4">0</read_table_fails><write_column_fails type="u64" identifier="5">0</write_column_fails><write_batch_column_fails type="u64" identifier="6">0</write_batch_column_fails><read_column_fails type="u64" identifier="7">0</read_column_fails></DbErrors></list></errors><statistics_table_info type="list" identifier="5" tags=".table_name"><list type="struct" size="3"><DbTableInfo><table_name type="string" identifier="1">ComputeCpuState:cpu_info</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">5</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">FieldNames:fields</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">96</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo><DbTableInfo><table_name type="string" identifier="1">VrouterStatsAgent:flow_rate</table_name><reads type="u64" identifier="2">0</reads><read_fails type="u64" identifier="3">0</read_fails><writes type="u64" identifier="4">4</writes><write_fails type="u64" identifier="5">0</write_fails></DbTableInfo></list></statistics_table_info></GeneratorDbStats></data></GeneratorDbStatsUve>', u'Type': 6, u'ModuleId': u'contrail-collector'}
],
3: [
{u'ObjectId': u'virtual_network:default-domain:admin:vn1-take2'}
],
4: [
{u'ObjectLog': u'<VncApiConfigLog type="sandesh"><api_log type="struct" identifier="1"><VncApiCommon><object_type type="string" identifier="2" key="ConfigObjectTable">virtual_network</object_type><identifier_name type="string" identifier="3" key="ConfigObjectTable">default-domain:admin:vn1-take2</identifier_name><url type="string" identifier="4">http://127.0.0.1:9100/virtual-networks</url><operation type="string" identifier="5">post</operation><useragent type="string" identifier="6">a6s45:/usr/bin/contrail-api</useragent><remote_ip type="string" identifier="7">127.0.0.1:9100</remote_ip><body type="string" identifier="9">{&apos;virtual-network&apos;: {&apos;fq_name&apos;: [&apos;default-domain&apos;, &apos;admin&apos;, &apos;vn1-take2&apos;], &apos;uuid&apos;: None, &apos;network_policy_refs&apos;: [], &apos;router_external&apos;: False, &apos;parent_type&apos;: &apos;project&apos;, &apos;id_perms&apos;: {u&apos;enable&apos;: True, u&apos;uuid&apos;: None, u&apos;creator&apos;: None, u&apos;created&apos;: 0, u&apos;user_visible&apos;: True, u&apos;last_modified&apos;: 0, u&apos;permissions&apos;: {u&apos;owner&apos;: u&apos;cloud-admin&apos;, u&apos;owner_access&apos;: 7, u&apos;other_access&apos;: 7, u&apos;group&apos;: u&apos;cloud-admin-group&apos;, u&apos;group_access&apos;: 7}, u&apos;description&apos;: None}, &apos;display_name&apos;: &apos;vn1-take2&apos;, &apos;is_shared&apos;: False}}</body><domain type="string" identifier="10" key="ConfigObjectTableByUser">default-domain</domain></VncApiCommon></api_log></VncApiConfigLog>', u'Messagetype': u'VncApiConfigLog', u'Source': u'a6s45', u'MessageTS': 1442434711187905, u'SystemLog': None, u'ModuleId': u'contrail-api'}
],
5: [
{u'Category': None, u'NodeType': u'Config', u'Level': 2147483647, u'InstanceId': u'0', u'Messagetype': u'UveVirtualNetworkConfigTrace', u'Source': u'nodec39', u'SequenceNum': 6867683, u'MessageTS': 1442429588898861, u'Xmlmessage': u'<UveVirtualNetworkConfigTrace type="sandesh"><data type="struct" identifier="1"><UveVirtualNetworkConfig><name type="string" identifier="1" key="ObjectVNTable">default-domain:demo:svc-vn-left</name><connected_networks type="list" identifier="4" aggtype="union"><list type="string" size="0" /></connected_networks><partially_connected_networks type="list" identifier="5" aggtype="union"><list type="string" size="0" /></partially_connected_networks><routing_instance_list type="list" identifier="6" aggtype="union"><list type="string" size="1"><element>svc-vn-left</element></list></routing_instance_list><total_acl_rules type="i32" identifier="7">0</total_acl_rules></UveVirtualNetworkConfig></data></UveVirtualNetworkConfigTrace>', u'Type': 6, u'ModuleId': u'contrail-schema'}
],
6: [
{u'Category': None, u'NodeType': u'Config', u'Level': 2147483647, u'InstanceId': u'0', u'Messagetype': u'UveVirtualNetworkConfigTrace', u'Source': u'a6s45', u'SequenceNum': 6867683, u'MessageTS': 1442429588898861, u'Xmlmessage': u'<UveVirtualNetworkConfigTrace type="sandesh"><data type="struct" identifier="1"><UveVirtualNetworkConfig><name type="string" identifier="1" key="ObjectVNTable">default-domain:demo:svc-vn-left</name><connected_networks type="list" identifier="4" aggtype="union"><list type="string" size="0" /></connected_networks><partially_connected_networks type="list" identifier="5" aggtype="union"><list type="string" size="0" /></partially_connected_networks><routing_instance_list type="list" identifier="6" aggtype="union"><list type="string" size="1"><element>svc-vn-left</element></list></routing_instance_list><total_acl_rules type="i32" identifier="7">0</total_acl_rules></UveVirtualNetworkConfig></data></UveVirtualNetworkConfigTrace>', u'Type': 6, u'ModuleId': u'contrail-schema'}
]
}

class LogQuerierTest(unittest.TestCase):

    @staticmethod
    def custom_post_url_http(url, params):
        global query_list
        query_list.append(json.loads(params))
        return '{"href": "/analytics/query/a415fe1e-51cb-11e5-aab0-00000a540d2d"}'

    @staticmethod
    def custom_get_query_result(opserver_ip, opserver_port, qid):
        try:
            return query_result[test_num]
        except KeyError:
            return []

    def custom_display(self, result):
        if result == [] or result is None:
            return
        try:
            self.assertTrue(result == query_result[test_num])
        except KeyError:
            self.assertTrue(False)

    def custom_process_start(self):
        self._querier.display(self._querier.read_result(query_result[test_num]))

    def custom_process_end(self):
        return

    def setUp(self):
        self.maxDiff = None
        self._querier = LogQuerier()

        flexmock(OpServerUtils).should_receive('post_url_http').replace_with(lambda x, y, w, z: self.custom_post_url_http(x, y))
        self.query_expectations = flexmock(OpServerUtils).should_receive('get_query_result').replace_with(lambda x, y, z, a, b: self.custom_get_query_result(x, y, z))
        self.display_expectations = flexmock(LogQuerier).should_receive('display').replace_with(lambda x: self.custom_display(x))
        self.process_start_expectations = flexmock(Process).should_receive('start').replace_with(lambda:self.custom_process_start())
        self.process_end_expectations = flexmock(Process).should_receive('join').replace_with(lambda:self.custom_process_end())

    #@unittest.skip("skip test_1_no_arg")
    def test_1_no_arg(self):
        global test_num
        global query_list
        query_list = []
        test_num = 1

        argv = sys.argv
        sys.argv = "contrail-logs".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"sort": 1, "sort_fields": ["MessageTS"], "select_fields": ["MessageTS", "Source", "ModuleId", "Category", "Messagetype", "SequenceNum", "Xmlmessage", "Type", "Level", "NodeType", "InstanceId"], "table": "MessageTable"}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_list[0]['end_time']) - int(query_list[0]['start_time']),10*60*pow(10,6))
        del query_list[0]['start_time']
        del query_list[0]['end_time']
        self.assertEqual(expected_result_dict, query_list[0])
        self.assertEqual(self.query_expectations.times_called,1)
        self.assertEqual(self.display_expectations.times_called,2)
        self.assertEqual(self.process_start_expectations.times_called,1)

    # a few args
    #@unittest.skip("skip test_2_message_query")
    def test_2_message_query(self):
        global test_num
        global query_list
        query_list = []
        test_num = 2

        argv = sys.argv
        sys.argv = "contrail-logs --source a6s45 --node-type Analytics --module contrail-collector --instance-id 0 --message-type GeneratorDbStatsUve".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"sort": 1, "sort_fields": ["MessageTS"], "select_fields": ["MessageTS", "Source", "ModuleId", "Category", "Messagetype", "SequenceNum", "Xmlmessage", "Type", "Level", "NodeType", "InstanceId"], "table": "MessageTable", "where": [[{"suffix": null, "value2": null, "name": "Source", "value": "a6s45", "op": 1}, {"suffix": null, "value2": null, "name": "ModuleId", "value": "contrail-collector", "op": 1}, {"suffix": null, "value2": null, "name": "Messagetype", "value": "GeneratorDbStatsUve", "op": 1}]], "filter": [[{"suffix": null, "value2": null, "name": "NodeType", "value": "Analytics", "op": 1}, {"suffix": null, "value2": null, "name": "InstanceId", "value": 0, "op": 1}]]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_list[0]['end_time']) - int(query_list[0]['start_time']),10*60*pow(10,6))
        del query_list[0]['start_time']
        del query_list[0]['end_time']
        self.assertEqual(expected_result_dict, query_list[0])
        self.assertEqual(self.query_expectations.times_called,1)
        self.assertEqual(self.display_expectations.times_called,2)
        self.assertEqual(self.process_start_expectations.times_called,1)

    # a object values query
    #@unittest.skip("skip test_3_object_value")
    def test_3_object_value(self):
        global test_num
        global query_list
        query_list = []
        test_num = 3

        argv = sys.argv
        sys.argv = "contrail-logs --object-type config --object-values".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"table": "ConfigObjectTable", "select_fields": ["ObjectId"]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_list[0]['end_time']) - int(query_list[0]['start_time']),10*60*pow(10,6))
        del query_list[0]['start_time']
        del query_list[0]['end_time']
        self.assertEqual(expected_result_dict, query_list[0])
        self.assertEqual(self.query_expectations.times_called,1)
        self.assertEqual(self.display_expectations.times_called,2)
        self.assertEqual(self.process_start_expectations.times_called,1)

    # a object id query
    #@unittest.skip("skip test_4_object_id")
    def test_4_object_id(self):
        global test_num
        global query_list
        query_list = []
        test_num = 4

        argv = sys.argv
        sys.argv = "contrail-logs --object-type config --object-id virtual_network:default-domain:admin:vn1-take2".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"sort": 1, "sort_fields": ["MessageTS"], "select_fields": ["MessageTS", "Source", "ModuleId", "Messagetype", "ObjectLog", "SystemLog"], "table": "ConfigObjectTable", "where": [[{"suffix": null, "value2": null, "name": "ObjectId", "value": "virtual_network:default-domain:admin:vn1-take2", "op": 1}]]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_list[0]['end_time']) - int(query_list[0]['start_time']),10*60*pow(10,6))
        del query_list[0]['start_time']
        del query_list[0]['end_time']
        self.assertEqual(expected_result_dict, query_list[0])
        self.assertEqual(self.query_expectations.times_called,1)
        self.assertEqual(self.display_expectations.times_called,2)
        self.assertEqual(self.process_start_expectations.times_called,1)

    # prefix query
    #@unittest.skip("skip test_5_prefix_query")
    def test_5_prefix_query(self):
        global test_num
        global query_list
        query_list = []
        test_num = 5

        argv = sys.argv
        sys.argv = "contrail-logs --source node* --message-type UveVirtualNetwork*".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"sort": 1, "sort_fields": ["MessageTS"], "select_fields": ["MessageTS", "Source", "ModuleId", "Category", "Messagetype", "SequenceNum", "Xmlmessage", "Type", "Level", "NodeType", "InstanceId"], "table": "MessageTable", "where": [[{"suffix": null, "value2": null, "name": "Source", "value": "node", "op": 7}, {"suffix": null, "value2": null, "name": "Messagetype", "value": "UveVirtualNetwork", "op": 7}]]}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(int(query_list[0]['end_time']) - int(query_list[0]['start_time']),10*60*pow(10,6))
        del query_list[0]['start_time']
        del query_list[0]['end_time']
        self.assertEqual(expected_result_dict, query_list[0])
        self.assertEqual(self.query_expectations.times_called,1)
        self.assertEqual(self.display_expectations.times_called,2)
        self.assertEqual(self.process_start_expectations.times_called,1)
    # end test_5_prefix_query

    #@unittest.skip("skip test_6_long_query")
    def test_6_long_query(self):
        global test_num
        global query_list
        query_list = []
        test_num = 6

        argv = sys.argv
        sys.argv = "contrail-logs --start-time now-30m --end-time now".split()
        self._querier.run()
        sys.argv = argv

        expected_result_str = '{"sort": 1, "sort_fields": ["MessageTS"], "select_fields": ["MessageTS", "Source", "ModuleId", "Category", "Messagetype", "SequenceNum", "Xmlmessage", "Type", "Level", "NodeType", "InstanceId"], "table": "MessageTable"}'
        expected_result_dict = json.loads(expected_result_str)
        self.assertEqual(self.query_expectations.times_called,3)
        self.assertEqual(self.display_expectations.times_called,4)
        self.assertEqual(self.process_start_expectations.times_called,3)
        for i in range(len(query_list) - 1):
            self.assertEqual(int(query_list[i]['end_time']) - int(query_list[i]['start_time']),10*60*pow(10,6))
            del query_list[i]['start_time']
            del query_list[i]['end_time']
            self.assertEqual(expected_result_dict, query_list[i])
        self.assertEqual(int(query_list[2]['end_time']) - int(query_list[2]['start_time']),10*60*pow(10,6) - 2)
        del query_list[2]['start_time']
        del query_list[2]['end_time']
        self.assertEqual(expected_result_dict, query_list[2])

if __name__ == '__main__':
    unittest.main()
