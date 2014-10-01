#!/usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# stats
#
# Query StatsOracle info from analytics
#

import sys
import argparse
import json
import datetime
from opserver_util import OpServerUtils
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames
import sandesh.viz.constants as VizConstants
from pysandesh.gen_py.sandesh.ttypes import SandeshType, SandeshLevel

STAT_TABLE_LIST = [xx.stat_type + "." + xx.stat_attr for xx in VizConstants._STAT_TABLES]


class StatQuerier(object):

    def __init__(self):
        self._args = None
    # end __init__

    # Public functions
    def parse_args(self):
        """ 
        Eg. python stats.py --analytics-api-ip 127.0.0.1
                          --analytics-api-port 8081
                          --table AnalyticsCpuState.cpu_info
                          --where name=a6s40 cpu_info.module_id=Collector
                          --select "T=60 SUM(cpu_info.cpu_share)"
                          --sort "SUM(cpu_info.cpu_share)"
                          [--start-time now-10m --end-time now] | --last 10m

            python stats.py --table AnalyticsCpuState.cpu_info
        """
        defaults = {
            'analytics_api_ip': '127.0.0.1',
            'analytics_api_port': '8081',
            'start_time': 'now-10m',
            'end_time': 'now',
            'select' : [],
            'where' : [],
            'sort': []
        }

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument("--analytics-api-ip", help="IP address of Analytics API Server")
        parser.add_argument("--analytics-api-port", help="Port of Analytcis API Server")
        parser.add_argument(
            "--start-time", help="Logs start time (format now-10m, now-1h)")
        parser.add_argument("--end-time", help="Logs end time")
        parser.add_argument(
            "--last", help="Logs from last time period (format 10m, 1d)")
        parser.add_argument(
            "--table", help="StatTable to query", choices=STAT_TABLE_LIST)
        parser.add_argument(
            "--dtable", help="Dynamic StatTable to query")
        parser.add_argument(
            "--select", help="List of Select Terms", nargs='+')
        parser.add_argument(
            "--where", help="List of Where Terms to be ANDed", nargs='+')
        parser.add_argument(
            "--sort", help="List of Sort Terms", nargs='+')
        self._args = parser.parse_args()

        if self._args.table is None and self._args.dtable is None:
            return -1

        try:
            self._start_time, self._end_time = \
                OpServerUtils.parse_start_end_time(
                    start_time = self._args.start_time,
                    end_time = self._args.end_time,
                    last = self._args.last)
        except:
            return -1

        return 0
    # end parse_args

    # Public functions
    def query(self):
        query_url = OpServerUtils.opserver_query_url(
            self._args.analytics_api_ip,
            self._args.analytics_api_port)

        if self._args.dtable is not None:
            rtable = self._args.dtable
        else:
            rtable = self._args.table
 
        query_dict = OpServerUtils.get_query_dict(
                "StatTable." + rtable, str(self._start_time), str(self._end_time),
                select_fields = self._args.select,
                where_clause = "AND".join(self._args.where),
                sort_fields = self._args.sort)
        
        print json.dumps(query_dict)
        resp = OpServerUtils.post_url_http(
            query_url, json.dumps(query_dict), sync = True)

        res = None
        if resp is not None:
            res = json.loads(resp)
            res = res['value']

        return res
    # end query

    def display(self, result):
        if result == [] or result is None:
            return
        for res in result:
            print res
    # end display

# end class StatQuerier


def main():
    querier = StatQuerier()
    if querier.parse_args() != 0:
        return


    if len(querier._args.select)==0 and querier._args.dtable is None: 
        tab_url = "http://" + querier._args.analytics_api_ip + ":" +\
            querier._args.analytics_api_port +\
            "/analytics/table/StatTable." + querier._args.table
        schematxt = OpServerUtils.get_url_http(tab_url + "/schema")
        schema = json.loads(schematxt.text)['columns']
        for pp in schema:
            if pp.has_key('suffixes') and pp['suffixes']:
                des = "%s %s" % (pp['name'],str(pp['suffixes']))
            else:
                des = "%s" % pp['name']
            if pp['index']:
                valuetxt = OpServerUtils.get_url_http(tab_url + "/column-values/" + pp['name'])
                print "%s : %s %s" % (des,pp['datatype'], valuetxt.text)
            else:
                print "%s : %s" % (des,pp['datatype'])
    else:
        result = querier.query()
        querier.display(result)
# end main

if __name__ == "__main__":
    main()
