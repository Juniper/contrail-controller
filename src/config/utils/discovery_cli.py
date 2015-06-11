#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import argparse
import requests
import json
import os
import sys

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--server', help="Discovery server address in the form IP:Port",
        default = '127.0.0.1:5998')
    parser.add_argument(
        '--op', choices = ['up', 'down', 'load-balance'], 
        help="Perform specified operation")
    parser.add_argument(
        '--service-id', help="Service id")
    parser.add_argument(
        '--service-type', help="Service type")

    args = parser.parse_args()
    return args
# end parse_args

args = parse_args()

# Validate Discovery server information
server = args.server.split(':')
if len(server) != 2:
    print 'Discovery server address must be of the form ip:port, '\
          'for example 127.0.0.1:5998'
    sys.exit(1)
server_ip = server[0]
server_port = server[1]

print 'Discovery Server = ', args.server

if args.op == 'up' or args.op == 'down':
    if not args.service_id or not args.service_type:
        print 'Please specify service type and ID'
        sys.exit(1)
    print 'Service type %s, id %s' % (args.service_type, args.service_id)
    data = {
        "service_type": args.service_type,
        "admin_state": args.op,
    }
    headers = {
        'Content-type': 'application/json',
    }
    url = "http://%s:%s/service/%s" % (server_ip, server_port, args.service_id)
    r = requests.put(url, data=json.dumps(data), headers=headers)
    if r.status_code != 200:
        print "Operation status %d" % r.status_code
elif args.op == 'load-balance':
    if not args.service_type:
        print 'Please specify service type'
        sys.exit(1)
    if args.service_id:
        print 'Specific service id %s ignored for this operation' % args.service_id
    url = "http://%s:%s/load-balance/%s" % (server_ip, server_port, args.service_type)
    r = requests.get(url)
    if r.status_code != 200:
        print "Operation status %d" % r.status_code
