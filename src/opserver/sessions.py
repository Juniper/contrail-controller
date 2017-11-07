#!/usr/bin/python

#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

#
# sessions
#
# Query SessionsTable info from analytics
#

import sys
import os
import ConfigParser
import argparse
import json
import re
from opserver_util import OpServerUtils
import sandesh.viz.constants as VizConstants

class SessionQuerier(object):

    def __init__(self):
        self._args = None
    # end __init__

    # public functions
    def run(self):
        if self.parse_args() != 0:
            return

        valid_select_list = []
        valid_where_list = []

        for table in VizConstants._TABLES:
            if table.name == self._args.table:
                valid_select_list = [column.name for column in table.schema.columns]
                valid_where_list = [column.name for column in table.schema.columns \
                                if column.index == True ]

        if not self._args.select:
            print "Enter valid select fields from " + str(valid_select_list)
            return
        else:
            for s in self._args.select:
                if s not in valid_select_list:
                    if 'T=' in s and self._args.table == 'SessionSeriesTable':
                        continue
                    print '%s is not a valid select field'
                    print 'List of valid select_fields' + str(valid_select_list)
        if self._args.where:
            for where in self._args.where:
                where_s = where.strip(' ()')
                where_e = re.split('=|<|>', where_s, 1)
                if where_e[0] not in valid_where_list:
                    print '%s is not a valid where field' % where_e[0]
                    print 'List of valid where fields' + str(valid_where_list)
                    return

        if self._args.filter:
            for filter in self._args.filter:
                filter_s = filter.strip(' ()')
                filter_e = re.split('=|<|>', filter_s, 1)
                if filter_e not in self._args.select:
                    print '%s is not a valid filter field' % filter_e[0]
                    print 'filter field should be present in select'

        if self._args.sort:
            for sort_field in self._args.sort:
                if sort_field not in self._args.select:
                    print '%s is not a valid sort field' % sort_field
                    print 'sort field should be present in select'
        result = self.query()
        self.display(result)

    def parse_args(self):
        """
        Eg. python sessions.py --analytics-api-ip 127.0.0.1
                          --analytics-api-port 8181
                          --table SessionSeriesTable
                          --where server_port=8080 vn=default-domain:admin:vn3
                          --select "T=60 SUM(forward_sampled_bytes) SUM(forward_sampled_pkts) \
                                    local_ip protocol application site"
                          [--start-time now-10m --end-time now] | --last 10m
                          --session-type client
        """
        defaults = {
            'analytics_api_ip': '127.0.0.1',
            'analytics_api_port': '8181',
            'start_time': 'now-10m',
            'end_time': 'now',
            'select' : [],
            'sort': [],
            'admin_user': 'admin',
            'admin_password': 'contrail123',
            'conf_file': '/etc/contrail/contrail-keystone-auth.conf',
            'is_service_instance': 0
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
            "--table", help="SessionAPI to query", required=True,
            choices=['SessionSeriesTable', 'SessionRecordTable'])
        parser.add_argument(
            "--session-type", help="Session Type", required=True,
            choices=['client', 'server'])
        parser.add_argument(
            "--is-service-instance", help="Service Instance Sessions", type=int)
        parser.add_argument(
            "--select", help="List of Select Terms", nargs='+')
        parser.add_argument(
            "--where", help="List of Where Terms to be ANDed", nargs='+')
        parser.add_argument(
            "--filter", help="List of Filter Terms to be ANDed", nargs='+')
        parser.add_argument(
            "--sort", help="List of Sort Terms", nargs='+')
        parser.add_argument(
            "--limit", help="Limit the number of results")

        self._args = parser.parse_args(remaining_argv)

        self._args.admin_user = args.admin_user
        self._args.admin_password = args.admin_password
        self._args.analytics_api_ip = args.analytics_api_ip
        self._args.analytics_api_port = args.analytics_api_port

        try:
            self._start_time, self._end_time = \
                OpServerUtils.parse_start_end_time(
                    start_time = self._args.start_time,
                    end_time = self._args.end_time,
                    last = self._args.last)
        except:
            return -1

        return 0
    #end parse_args

    def query(self):
        if not self._args.where:
            where = ''
        else:
            where = "AND".join(self._args.where)

        if not self._args.filter:
            filter = None
        else:
            filter = "AND".join(self._args.filter)

        query_url = OpServerUtils.opserver_query_url(
            self._args.analytics_api_ip,
            self._args.analytics_api_port)

        query_dict = OpServerUtils.get_query_dict(
                self._args.table, str(self._start_time), str(self._end_time),
                select_fields = self._args.select,
                where_clause = where,
                filter = filter,
                sort_fields = self._args.sort,
                limit=self._args.limit,
                session_type=self._args.session_type,
                is_service_instance=self._args.is_service_instance)

        print json.dumps(query_dict)
        resp = OpServerUtils.post_url_http(
            query_url, json.dumps(query_dict), self._args.admin_user,
            self._args.admin_password)

        res = None
        if resp is not None:
            res = json.loads(resp)
            res = res['value']

        return res
    # end query

    def display(self, result):
        if not result:
            return
        for res in result:
            print res
    #end display

#end class SessionsQuerier

def main():
    querier = SessionQuerier()
    querier.run()
#end main

if __name__ == "__main__":
    main()
