#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import sys
import json
import argparse
import requests
import urllib2
from prettytable import PrettyTable
import datetime

class ContrailDb(object):

    def __init__(self):
        self._args = None
    # end __init__

    def parse_args(self):
        """
        Eg. python db.py --analytics-api-ip 127.0.0.1
                         --analytics-api-port 8081
                         --show
                         --purge
        """
        defaults = {
            'analytics_api_ip': '127.0.0.1',
            'analytics_api_port': '8081',
        }

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument("--analytics-api-ip",
                            help="IP address of Analytics API Server")
        parser.add_argument("--analytics-api-port",
                            help="Port of Analytics API Server")
        group = parser.add_mutually_exclusive_group()
        group.add_argument("--show", action='store_true',
                            help="show database usage and purge stats")
        self._args = parser.parse_args()
        return 0
    # end parse_args

    def contrail_db_show(self):
        uves = ["DatabaseUsageInfo", "DatabasePurgeInfo"]
        for uve in uves:
            try:
                url = "http://%s:%s/analytics/uves/database-nodes?cfilt=%s" \
                    % (self._args.analytics_api_ip,
                       self._args.analytics_api_port, uve)
                node_dburls = json.loads(urllib2.urlopen(url).read())
                for node_dburl in node_dburls:
                    stats = json.loads(
                                urllib2.urlopen(node_dburl['href']).read())
                    db1 = json.loads(
                              json.dumps({'Hostname': node_dburl['name']}))
                    if "DatabaseUsageInfo" in uve:
                        db2 = stats[uve]['database_usage'][0]
                    elif "DatabasePurgeInfo" in uve:
                        db2 = stats[uve]['stats'][0]
                        db2['request_time'] = datetime.datetime.fromtimestamp(
                            db2['request_time']*(10**(-6))).strftime(
                            '%Y-%m-%d %H:%M:%S.%f')
                        db2['duration'] = datetime.timedelta(
                            microseconds=db2['duration'])
                    x = PrettyTable((db1.keys()+db2.keys()))
                    x.add_row(db1.values()+db2.values())
                    print x
            except Exception as e:
                print "Exception: Unable to fetch db information. %s" % e
    # end contrail_db_show

# end class ContrailDb

def main(args_str=None):
    try:
        db = ContrailDb()
        if db.parse_args() != 0:
             return

        if db._args.show:
             db.contrail_db_show()

    except KeyboardInterrupt:
        return
# end main

if __name__ == "__main__":
    main()
