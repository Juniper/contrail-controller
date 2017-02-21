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
import os
import ConfigParser
import argparse
import json
from opserver_util import OpServerUtils
import sandesh.viz.constants as VizConstants

class StatQuerier(object):

    def __init__(self):
        self._args = None
    # end __init__

    # Public functions
    def run(self):
        if self.parse_args() != 0:
            return

        if len(self._args.select)==0 and self._args.dtable is None: 
            tab_url = "http://" + self._args.analytics_api_ip + ":" +\
                self._args.analytics_api_port +\
                "/analytics/table/StatTable." + self._args.table
            schematxt = OpServerUtils.get_url_http(tab_url + "/schema",
                self._args.admin_user, self._args.admin_password)
            schema = json.loads(schematxt.text)['columns']
            for pp in schema:
                if pp.has_key('suffixes') and pp['suffixes']:
                    des = "%s %s" % (pp['name'],str(pp['suffixes']))
                else:
                    des = "%s" % pp['name']
                if pp['index']:
                    valuetxt = OpServerUtils.get_url_http(
                        tab_url + "/column-values/" + pp['name'],
                        self._args.admin_user, self._args.admin_password)
                    print "%s : %s %s" % (des,pp['datatype'], valuetxt.text)
                else:
                    print "%s : %s" % (des,pp['datatype'])
        else:
            result = self.query()
            self.display(result)

    def parse_args(self):
        """ 
        Eg. python stats.py --analytics-api-ip 127.0.0.1
                          --analytics-api-port 8181
                          --table NodeStatus.process_mem_cpu_usage
                          --where name=a6s40 cpu_info.module_id=Collector
                          --select "T=60 SUM(cpu_info.cpu_share)"
                          --sort "SUM(cpu_info.cpu_share)"
                          [--start-time now-10m --end-time now] | --last 10m

            python stats.py --table NodeStatus.process_mem_cpu_usage
        """
        defaults = {
            'analytics_api_ip': '127.0.0.1',
            'analytics_api_port': '8181',
            'start_time': 'now-10m',
            'end_time': 'now',
            'select' : [],
            'where' : ['Source=*'],
            'sort': [],
            'admin_user': 'admin',
            'admin_password': 'contrail123',
            'conf_file': '/etc/contrail/contrail-keystone-auth.conf',
        }

        conf_parser = argparse.ArgumentParser(add_help=False)
        conf_parser.add_argument("--admin-user", help="Name of admin user")
        conf_parser.add_argument("--admin-password", help="Password of admin user")
        conf_parser.add_argument("--conf-file", help="Configuration file")
        conf_parser.add_argument("--analytics-api-ip", help="IP address of Analytics API Server")
        conf_parser.add_argument("--analytics-api-port", help="Port of Analytcis API Server")
        args, remaining_argv = conf_parser.parse_known_args();

        configfile = defaults['conf_file']
        if args.conf_file:
            configfile = args.conf_file

        config = ConfigParser.SafeConfigParser()
        config.read(configfile)
        if 'KEYSTONE' in config.sections():
            if args.admin_user == None:
                args.admin_user = config.get('KEYSTONE', 'admin_user')
            if args.admin_password == None:
                args.admin_password = config.get('KEYSTONE','admin_password')

        if args.admin_user == None:
            args.admin_user = defaults['admin_user']
        if args.admin_password == None:
            args.admin_password = defaults['admin_password']

        if args.analytics_api_ip == None:
            args.analytics_api_ip = defaults['analytics_api_ip']
        if args.analytics_api_port == None:
            args.analytics_api_port = defaults['analytics_api_port']

        stat_table_list = [xx.stat_type + "." + xx.stat_attr for xx in VizConstants._STAT_TABLES]
        tab_url = "http://" + args.analytics_api_ip + ":" +\
            args.analytics_api_port + "/analytics/tables"
        tables = OpServerUtils.get_url_http(tab_url,
            args.admin_user, args.admin_password)
        if tables != {}:
            if tables.status_code == 200:
                table_list = json.loads(tables.text)
                for table in table_list:
                    if table['type'] == 'STAT':
                        table_name = '.'.join(table['name'].split('.')[1:])
                        # append to stat_table_list only if not existing
                        if table_name not in stat_table_list:
                            stat_table_list.append(table_name)

        parser = argparse.ArgumentParser(
                  # Inherit options from config_parser
                  parents=[conf_parser],
                  # print script description with -h/--help
                  description=__doc__,
                  formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument(
            "--start-time", help="Logs start time (format now-10m, now-1h)")
        parser.add_argument("--end-time", help="Logs end time")
        parser.add_argument(
            "--last", help="Logs from last time period (format 10m, 1d)")
        parser.add_argument(
            "--table", help="StatTable to query", choices=stat_table_list)
        parser.add_argument(
            "--dtable", help="Dynamic StatTable to query")
        parser.add_argument(
            "--select", help="List of Select Terms", nargs='+')
        parser.add_argument(
            "--where", help="List of Where Terms to be ANDed", nargs='+')
        parser.add_argument(
            "--sort", help="List of Sort Terms", nargs='+')
        self._args = parser.parse_args(remaining_argv)

        self._args.admin_user = args.admin_user
        self._args.admin_password = args.admin_password
        self._args.analytics_api_ip = args.analytics_api_ip
        self._args.analytics_api_port = args.analytics_api_port

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
            query_url, json.dumps(query_dict), self._args.admin_user,
            self._args.admin_password, sync = True)

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
    querier.run()
# end main

if __name__ == "__main__":
    main()
