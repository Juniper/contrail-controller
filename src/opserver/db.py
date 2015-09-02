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
        group.add_argument("--purge", help='Purge data from contrail-db')
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

    def start_purge(self, params):
        url = "http://%s:%s/analytics/operation/database-purge" \
            % (self._args.analytics_api_ip, self._args.analytics_api_port)
        hdrs = {'Content-type': 'application/json; charset="UTF-8"',
                'Expect': '202-accepted'}
        try:
            response = requests.post(url,
                       data=json.dumps({'purge_input': params}), headers=hdrs)
            res = json.loads(response.text)
            if ('purge_input' in res.keys()):
                del res['purge_input']
            if ('purge_start_time' in res.keys()):
                res['purge_start_time'] = datetime.datetime.fromtimestamp(
                    int(res['purge_start_time'])*(10**(-6))).strftime(
                    '%Y-%m-%d %H:%M:%S.%f')
            for key in res:
                print "%s : %s" % (key, res[key])
        except requests.exceptions.ConnectionError, e:
            print "Connection to %s failed %s" % (url, str(e))
    # end start_purge

# end class ContrailDb

def main(args_str=None):
    try:
        db = ContrailDb()
        if db.parse_args() != 0:
             return

        if db._args.show:
             db.contrail_db_show()

        if db._args.purge:
            if (db._args.purge.isdigit()):
                input = int(db._args.purge)
            else:
                input = db._args.purge
            db.start_purge(input)
    except KeyboardInterrupt:
        return
# end main

if __name__ == "__main__":
    main()
