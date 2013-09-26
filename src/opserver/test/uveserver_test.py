#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# UVEServerTest
#
# Unit Tests for UVE Aggregation in Operational State Server
#

import os
import sys
import copy
import unittest
import pdb
import json

curfile = sys.path[0]
from opserver.uveserver import UVEServer
from opserver.uveserver import ParallelAggregator


class RedisMock(object):
    pass


def MakeBasic(typ, val, aggtype=None):
    item = {}
    item['@type'] = typ
    if isinstance(val, basestring):
        item['#text'] = val
    else:
        item['#text'] = str(val)
    if aggtype is not None:
        item['@aggtype'] = aggtype
    return item


def MakeList(typ, valname, val, aggtype=None):
    item = {}
    item['@type'] = "list"
    if aggtype is not None:
        item['@aggtype'] = aggtype
    item['list'] = {}
    item['list']['@size'] = "1"
    item['list']['@type'] = typ
    item['list'][valname] = val
    return item


def AppendList(lst, valname, item):
    result = copy.deepcopy(lst)
    if result['list']['@size'] == "1":
        result['list'][valname] = [result['list'][valname]]
    result['list'][valname].append(item)
    result['list']['@size'] = str(int(result['list']['@size']) + 1)
    return result


def MakeVnPolicyList(policies):
    result = {}
    for elems in policies:
        item = {}
        item['vnp_num'] = MakeBasic("i32", elems[0])
        item['vnp_name'] = MakeBasic("string", elems[1])
        if not result:
            result = MakeList("struct", "VnPolicy", item)
        else:
            result = AppendList(result, "VnPolicy", item)
    return result


def MakeVnStatList(stats):
    result = {}
    for elems in stats:
        item = {}
        item['other_vn'] = MakeBasic("string", elems[0], "listkey")
        item['bytes'] = MakeBasic("i64", elems[1])
        if not result:
            result = MakeList("struct", "VnStats", item, "append")
        else:
            result = AppendList(result, "VnStats", item)
    return result


def MakeStringList(strings):
    result = {}
    result['@type'] = "list"
    result['list'] = {}
    result['list']['@type'] = "string"
    result['list']['@size'] = "0"
    for elems in strings:
        if result['list']['@size'] == "0":
            result = MakeList("string", "element", elems, "union")
        else:
            result = AppendList(result, "element", elems)
    return result

'''
This function returns a mock sandesh dict
        1: string                           name (key="ObjectVNTable")
        2: optional list<VnPolicy>          attached_policies
        3: optional list<string>            connected_networks (
                                                aggtype="union")
        4: optional i32                     total_virtual_machines (
                                                aggtype="sum")
        5: optional i32                     total_acl_rules
        6: optional i64                     in_tpkts  (aggtype="counter")
        7: optional list<VnStats>           in_stats  (aggtype="append")
'''


def MakeUVEVirtualNetwork(
        istate,
        key,
        source,
        attached_policies=None,
        connected_networks=None,
        total_virtual_machines=None,
        total_acl_rules=None,
        in_tpkts=None,
        in_stats=None):
    rsult = copy.deepcopy(istate)
    if rsult is None:
        rsult = {}
        rsult[key] = {}
        rsult[key]['UVEVirtualNetwork'] = {}

    result = rsult[key]
    if attached_policies is not None:
        if ('attached_policies' not in result['UVEVirtualNetwork']):
            result['UVEVirtualNetwork']['attached_policies'] = {}
        result['UVEVirtualNetwork']['attached_policies'][source] = \
            MakeVnPolicyList(attached_policies)
    if connected_networks is not None:
        if ('connected_networks' not in result['UVEVirtualNetwork']):
            result['UVEVirtualNetwork']['connected_networks'] = {}
        result['UVEVirtualNetwork']['connected_networks'][source] = \
            MakeStringList(connected_networks)
    if total_virtual_machines is not None:
        if ('total_virtual_machines' not in result['UVEVirtualNetwork']):
            result['UVEVirtualNetwork']['total_virtual_machines'] = {}
        result['UVEVirtualNetwork']['total_virtual_machines'][source] = \
            MakeBasic("i32", total_virtual_machines, "sum")
    if total_acl_rules is not None:
        if ('total_acl_rules' not in result['UVEVirtualNetwork']):
            result['UVEVirtualNetwork']['total_acl_rules'] = {}
        result['UVEVirtualNetwork']['total_acl_rules'][source] = \
            MakeBasic("i32", total_acl_rules)
    if in_tpkts is not None:
        if ('in_tpkts' not in result['UVEVirtualNetwork']):
            result['UVEVirtualNetwork']['in_tpkts'] = {}
        result['UVEVirtualNetwork']['in_tpkts'][source] = \
            MakeBasic("i64", in_tpkts, "counter")
    if in_stats is not None:
        if ('in_stats' not in result['UVEVirtualNetwork']):
            result['UVEVirtualNetwork']['in_stats'] = {}
        result['UVEVirtualNetwork']['in_stats'][source] = \
            MakeVnStatList(in_stats)
    return rsult


class UVEServerTest(unittest.TestCase):

    def setUp(self):
        self._oss = UVEServer(0, 0)

    def tearDown(self):
        del self._oss

    def test_simple(self):
        print "*** Running test_simple ***"

        uvevn = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "10.10.10.10",
            attached_policies=[
                ("100", "allow-some"), ("200", "deny-others")],
            connected_networks=["vn-01", "vn-02", "vn-03"],
            in_stats=[("vn-01", "1000"), ("vn-02", "1800")],
        )
        pa = ParallelAggregator(uvevn)
        res = pa.aggregate("abc-corp:vn-00", False)
        print json.dumps(res, indent=4, sort_keys=True)

    def test_default_agg(self):
        print "*** Running test_default_agg ***"

        uvevn = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "10.10.10.10",
            attached_policies=[
                ("100", "allow-some"), ("200", "deny-others")],
            total_acl_rules=4
        )

        uvevn2 = MakeUVEVirtualNetwork(
            uvevn, "abc-corp:vn-00", "10.10.10.11",
            attached_policies=[
                ("100", "allow-some"), ("200", "deny-others")],
            total_acl_rules=5
        )

        pa = ParallelAggregator(uvevn2)
        res = pa.aggregate("abc-corp:vn-00", False)

        attached_policies = \
            uvevn["abc-corp:vn-00"]['UVEVirtualNetwork'][
                'attached_policies']["10.10.10.10"]
        self.assertEqual(attached_policies,
                         res['UVEVirtualNetwork']['attached_policies'][0][0])
        self.assertEqual(
            sorted(["10.10.10.10", "10.10.10.11"]),
            sorted(res['UVEVirtualNetwork']['attached_policies'][0][1:]))

        acl1 = uvevn["abc-corp:vn-00"]['UVEVirtualNetwork'][
            'total_acl_rules']["10.10.10.10"]
        acl2 = uvevn2["abc-corp:vn-00"]['UVEVirtualNetwork'][
            'total_acl_rules']["10.10.10.11"]
        self.assertEqual(
            sorted([acl1, acl2]),
            sorted([res['UVEVirtualNetwork']['total_acl_rules'][0][0],
                    res['UVEVirtualNetwork']['total_acl_rules'][1][0]]))
        self.assertEqual(
            sorted(["10.10.10.10", "10.10.10.11"]),
            sorted([res['UVEVirtualNetwork']['total_acl_rules'][0][1],
                    res['UVEVirtualNetwork']['total_acl_rules'][1][1]]))
        print json.dumps(res, indent=4, sort_keys=True)

    def test_union_agg(self):
        print "*** Running test_union_agg ***"

        uvevn = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "10.10.10.10",
            connected_networks=["vn-00"],
        )

        uvevn2 = MakeUVEVirtualNetwork(
            uvevn, "abc-corp:vn-00", "10.10.10.11",
            connected_networks=["vn-01", "vn-02", "vn-03"],
        )

        pa = ParallelAggregator(uvevn2)
        res = pa.aggregate("abc-corp:vn-00", False)

        print json.dumps(res, indent=4, sort_keys=True)

        for elem in res['UVEVirtualNetwork']['connected_networks']['list']:
            if elem[0] != '@':
                res['UVEVirtualNetwork']['connected_networks'][
                    'list'][elem] = \
                    sorted(res['UVEVirtualNetwork'][
                           'connected_networks']['list'][elem])

        uvetest = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "10.10.10.11",
            connected_networks=["vn-00", "vn-01", "vn-02", "vn-03"],
        )

        cn = uvetest["abc-corp:vn-00"]['UVEVirtualNetwork'][
            'connected_networks']["10.10.10.11"]
        self.assertEqual(cn, res['UVEVirtualNetwork']['connected_networks'])

    def test_sum_agg(self):
        print "*** Running test_sum_agg ***"

        uvevn = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "10.10.10.10",
            total_virtual_machines=4
        )

        uvevn2 = MakeUVEVirtualNetwork(
            uvevn, "abc-corp:vn-00", "10.10.10.11",
            total_virtual_machines=7
        )

        uvetest = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "10.10.10.10",
            total_virtual_machines=11
        )

        pa = ParallelAggregator(uvevn2)
        res = pa.aggregate("abc-corp:vn-00", False)

        print json.dumps(res, indent=4, sort_keys=True)

        cnt1 = uvetest["abc-corp:vn-00"]['UVEVirtualNetwork'][
            'total_virtual_machines']["10.10.10.10"]
        self.assertEqual(
            cnt1, res['UVEVirtualNetwork']['total_virtual_machines'])

    def test_counter_agg(self):
        print "*** Running test_counter_agg ***"

        uvevn = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "previous",
            in_tpkts=4
        )

        uvevn2 = MakeUVEVirtualNetwork(
            uvevn, "abc-corp:vn-00", "10.10.10.11",
            in_tpkts=7
        )

        uvevn3 = UVEServer.merge_previous(
            uvevn2, "abc-corp:vn-00", "UVEVirtualNetwork", "in_tpkts",
            uvevn["abc-corp:vn-00"]['UVEVirtualNetwork']['in_tpkts'][
                "previous"])

        pa = ParallelAggregator(uvevn3)
        res = pa.aggregate("abc-corp:vn-00", False)
        print json.dumps(res, indent=4, sort_keys=True)

        uvetest = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "sample",
            in_tpkts=15
        )
        in_tpkts = uvetest["abc-corp:vn-00"][
            'UVEVirtualNetwork']['in_tpkts']["sample"]

        self.assertEqual(in_tpkts, res['UVEVirtualNetwork']['in_tpkts'])

    def test_append_agg(self):
        print "*** Running test_append_agg ***"

        uvevn = MakeUVEVirtualNetwork(
            None, "abc-corp:vn-00", "previous",
            in_stats=[("vn-01", "1000"), ("vn-02", "1800")],
        )

        uvevn2 = MakeUVEVirtualNetwork(
            uvevn, "abc-corp:vn-00", "10.10.10.11",
            in_stats=[("vn-02", "1200"), ("vn-03", "1500")],
        )

        uveprev = MakeUVEVirtualNetwork(
            None,  "abc-corp:vn-00", "10.10.10.10",
            in_stats=[("vn-01", "1000"), ("vn-03", "1700")],
        )

        uvevn3 = UVEServer.merge_previous(
            uvevn2, "abc-corp:vn-00", "UVEVirtualNetwork", "in_stats",
            uveprev["abc-corp:vn-00"]['UVEVirtualNetwork'][
                'in_stats']["10.10.10.10"])

        pa = ParallelAggregator(uvevn3)
        res = pa.aggregate("abc-corp:vn-00", False)
        print json.dumps(res, indent=4, sort_keys=True)

        res['UVEVirtualNetwork']['in_stats']["list"]["VnStats"] = \
            sorted(res['UVEVirtualNetwork']['in_stats']["list"]["VnStats"])

        uvetest = MakeUVEVirtualNetwork(
            None,  "abc-corp:vn-00", "sample",
            in_stats=[("vn-01", "2000"), (
                "vn-02", "3000"), ("vn-03", "3200")],
        )

        uvetest["abc-corp:vn-00"]["UVEVirtualNetwork"]["in_stats"][
            "sample"]["list"]["VnStats"] = \
            sorted(uvetest["abc-corp:vn-00"]["UVEVirtualNetwork"][
                   "in_stats"]["sample"]["list"]["VnStats"])

        in_stats = uvetest["abc-corp:vn-00"][
            "UVEVirtualNetwork"]["in_stats"]["sample"]
        self.assertEqual(in_stats, res['UVEVirtualNetwork']['in_stats'])


if __name__ == '__main__':
    unittest.main()
