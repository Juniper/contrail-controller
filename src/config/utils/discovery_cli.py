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
        '--load-balance',  help="Load balance specified service type", action="store_true")
    parser.add_argument(
        '--oper-state', choices = ['up', 'down'],
        help="Set operational state of a service")
    parser.add_argument(
        '--oper-state-reason', help="Reason for down operational state of a service")
    parser.add_argument(
        '--admin-state', choices = ['up', 'down'],
        help="Set administrative state of a service")
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

if args.oper_state or args.admin_state or args.oper_state_reason:
    if not args.service_id or not args.service_type:
        print 'Please specify service type and ID'
        sys.exit(1)
    print 'Service type %s, id %s' % (args.service_type, args.service_id)
    data = {
        "service-type": args.service_type,
    }
    if args.oper_state:
        data['oper-state'] = args.oper_state
    if args.oper_state_reason:
        data['oper-state-reason'] = args.oper_state_reason
    if args.admin_state:
        data['admin-state'] = args.admin_state
    headers = {
        'Content-type': 'application/json',
    }
    url = "http://%s:%s/service/%s" % (server_ip, server_port, args.service_id)
    r = requests.put(url, data=json.dumps(data), headers=headers)
    if r.status_code != 200:
        print "Operation status %d" % r.status_code
elif args.load_balance:
    if not args.service_type:
        print 'Please specify service type'
        sys.exit(1)
    if args.service_id:
        print 'Specific service id %s ignored for this operation' % args.service_id
    url = "http://%s:%s/load-balance/%s" % (server_ip, server_port, args.service_type)
    r = requests.post(url)
    if r.status_code != 200:
        print "Operation status %d" % r.status_code
