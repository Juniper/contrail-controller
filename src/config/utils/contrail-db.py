#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from optparse import OptionParser
import sys
import json
import argparse
import requests
from sandesh_common.vns.constants import SERVICE_OPSERVER, \
    SERVICE_DATABASE_NODE_MGR, OpServerPort
sys.path.append('/usr/share/contrail/')
obj = __import__('contrail-status')

class ContrailDb(object):

    def __init__(self, ip):
       self._ip = ip

    def contrail_db_show(self, timeout):
        server_list = [SERVICE_OPSERVER, SERVICE_DATABASE_NODE_MGR]
        for server in server_list:
            port = obj.get_default_http_server_port(server, False)
            stats = obj.IntrospectUtil(self._ip, port, False,
                                    timeout)
            db_usage = stats.get_uve('DatabaseUsageInfo')
            if (db_usage):
                print 'database usage stats: %s' % db_usage[0]['database_usage']
            purge_stat = stats.get_uve('DatabasePurgeInfo')
            if (purge_stat):
                print 'purge stats: %s' % purge_stat[0]['stats']
    # end show_purge

    def start_purge(self, params):
        url = "http://%s:%d/analytics/operation/database-purge" \
               % (self._ip, OpServerPort)
        hdrs = {'Content-type': 'application/json; charset="UTF-8"',
                    'Expect': '202-accepted'}
        try:
            response = requests.post(url,
                       data=json.dumps({'purge_input': params}), headers=hdrs)
        except requests.exceptions.ConnectionError, e:
            print "Connection to %s failed %s" % (url, str(e))
        if (response.status_code == 202) or (response.status_code) == 200:
            print response.text
        else:
            print "HTTP error code: %d" % response.status_code
    # end start_purge

# end ContrailDb

def main(args_str=None):
    parser = OptionParser()
    parser.add_option('-s', '--show', action='store_true',
                      default=False, help="show database usage and purge stats")
    parser.add_option('-t', '--purge_time', dest='purge_time',
                      default=None, help='Purge all the data before purge time')
    parser.add_option('-p', '--purge_percent_input', dest='purge_percent_input',
                      type='int',
                      default=None, help='Purge input percentage of data')
    parser.add_option('-x', '--timeout', dest='timeout', type="float",
                      default=2,
                      help="timeout in seconds for HTTP requests to services")

    (options, args) = parser.parse_args()
    if args:
        parser.error("No arguments are permitted")
    db = ContrailDb('localhost')
    if (options.show):
        db.contrail_db_show(options.timeout)
    if (options.purge_time):
        db.start_purge(options.purge_time)
    if (options.purge_percent_input):
        db.start_purge(options.purge_percent_input)
# end main

if __name__ == "__main__":
    main()
