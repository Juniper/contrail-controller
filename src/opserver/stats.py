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
from opserver.opserver_util import OpServerUtils
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames
import opserver.sandesh.viz.constants as VizConstants
from pysandesh.gen_py.sandesh.ttypes import SandeshType, SandeshLevel

STAT_TABLE_LIST = [xx.stat_type + "." + xx.stat_attr for xx in VizConstants._STAT_TABLES]


class StatQuerier(object):

    def __init__(self):
        self._args = None
    # end __init__

    # Public functions
    def parse_args(self):
        """ 
        Eg. python stats.py --opserver-ip 127.0.0.1
                          --opserver-port 8081
                          --table AnalyticsCpuState.cpu_info
                          --where name=a6s40 cpu_info.module_id=Collector
                          --select "T=60 SUM(cpu_info.cpu_share)"
                          --sort "SUM(cpu_info.cpu_share)"
                          [--start-time now-10m --end-time now] | --last 10m

            python stats.py --table AnalyticsCpuState.cpu_info
        """
        defaults = {
            'opserver_ip': '127.0.0.1',
            'opserver_port': '8081',
            'start_time': 'now-10m',
            'end_time': 'now',
            'select' : [],
            'where' : [],
            'sort': []
        }

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument("--opserver-ip", help="IP address of OpServer")
        parser.add_argument("--opserver-port", help="Port of OpServer")
        parser.add_argument(
            "--start-time", help="Logs start time (format now-10m, now-1h)")
        parser.add_argument("--end-time", help="Logs end time")
        parser.add_argument(
            "--last", help="Logs from last time period (format 10m, 1d)")
        parser.add_argument(
            "--table", help="StatTable to query", choices=STAT_TABLE_LIST)
        parser.add_argument(
            "--select", help="List of Select Terms", nargs='+')
        parser.add_argument(
            "--where", help="List of Where Terms to be ANDed", nargs='+')
        parser.add_argument(
            "--sort", help="List of Sort Terms", nargs='+')
        self._args = parser.parse_args()

        if self._args.table is None:
            return -1

        if self._args.last is not None:
            self._args.last = '-' + self._args.last
            self._start_time = OpServerUtils.convert_to_utc_timestamp_usec(
                self._args.last)
            self._end_time = OpServerUtils.convert_to_utc_timestamp_usec('now')
        else:
            try:
                if (self._args.start_time.isdigit() and
                        self._args.end_time.isdigit()):
                    self._start_time = int(self._args.start_time)
                    self._end_time = int(self._args.end_time)
                else:
                    self._start_time =\
                        OpServerUtils.convert_to_utc_timestamp_usec(
                            self._args.start_time)
                    self._end_time =\
                        OpServerUtils.convert_to_utc_timestamp_usec(
                            self._args.end_time)
            except:
                print 'Incorrect start-time (%s) or end-time (%s) format' %\
                    (self._args.start_time, self._args.end_time)
                return -1
        return 0
    # end parse_args

    # Public functions
    def query(self,schema):
        query_url = OpServerUtils.opserver_query_url(
            self._args.opserver_ip,
            self._args.opserver_port)
        
        query_dict = OpServerUtils.get_query_dict(
                "StatTable." + self._args.table, str(self._start_time), str(self._end_time),
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

    tab_url = "http://" + querier._args.opserver_ip + ":" + querier._args.opserver_port +\
        "/analytics/table/StatTable." + querier._args.table
    schematxt = OpServerUtils.get_url_http(tab_url + "/schema")
    schema = json.loads(schematxt.text)['columns']
    if len(querier._args.select)==0: 
        for pp in schema:
            if pp['index']:
                valuetxt = OpServerUtils.get_url_http(tab_url + "/column-values/" + pp['name'])
                print "%s : %s %s" % (pp['name'],pp['datatype'], valuetxt.text)
            else:
                print "%s : %s" % (pp['name'],pp['datatype'])
    else:
        result = querier.query(schema)
        querier.display(result)
# end main

if __name__ == "__main__":
    main()
