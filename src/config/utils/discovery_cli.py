#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import uuid as __uuid
import argparse
import requests
import json
import os
import sys

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import *
from cfgm_common.exceptions import *

EP_DELIM=','
PUBSUB_DELIM=' '

def show_usage():
    print 'A rule string must be specified for this operation'
    print '<publisher-spec>  <subscriber-spec>'
    print 'publisher-spec := <prefix>,<type>,<id>,<version>'
    print 'subscriber-spec := <prefix>,<type>,<id>,<version>'

def parse_pubsub_ep(pubsub_str):
    r = pubsub_str.split(EP_DELIM)
    if len(r) < 4:
        for i in range(4-len(r)):
            r.append('')
    return r

# '1.1.1.1/24' or '1.1.1.1'
def prefix_str_to_obj(prefix_str):
    if '/' not in prefix_str:
        prefix_str += '/32'
    x = prefix_str.split('/')
    if len(x) != 2:
        return None
    return SubnetType(x[0], int(x[1]))

def build_dsa_rule_entry(rule_str):
    r = parse_pubsub_ep(rule_str)
    r = rule_str.split(PUBSUB_DELIM) if rule_str else []
    if len(r) < 2:
        return None

    # [0] is publisher-spec, [1] is subscriber-spec
    pubspec = parse_pubsub_ep(r[0])
    subspec = parse_pubsub_ep(r[1])

    pfx_pub = prefix_str_to_obj(pubspec[0])
    pfx_sub = prefix_str_to_obj(subspec[0])
    if pfx_sub is None or pfx_sub is None:
        return None

    publisher = DiscoveryPubSubEndPointType(ep_prefix = pfx_pub,
                    ep_type = pubspec[1], ep_id = pubspec[2],
                    ep_version = pubspec[3])
    subscriber = [DiscoveryPubSubEndPointType(ep_prefix = pfx_sub,
                     ep_type = subspec[1], ep_id = subspec[2],
                     ep_version = subspec[3])]
    dsa_rule_entry = DiscoveryServiceAssignmentType(publisher, subscriber)

    return dsa_rule_entry
#end

def match_pubsub_ep(ep1, ep2):
    if ep1.ep_prefix.ip_prefix != ep2.ep_prefix.ip_prefix:
        return False
    if ep1.ep_prefix.ip_prefix_len != ep2.ep_prefix.ip_prefix_len:
        return False
    if ep1.ep_type != ep2.ep_type:
        return False
    if ep1.ep_id != ep2.ep_id:
        return False
    if ep1.ep_version != ep2.ep_version:
        return False
    return True

# match two rules (type DiscoveryServiceAssignmentType)
def match_rule_entry(r1, r2):
    if not match_pubsub_ep(r1.get_publisher(), r2.get_publisher()):
        return False

    sub1 = r1.get_subscriber()
    sub2 = r2.get_subscriber()
    if len(sub1) != len(sub2):
        return False
    for i in range(len(sub1)):
        if not match_pubsub_ep(sub1[i], sub2[i]):
            return False
    return True
# end

# check if rule already exists in rule list and returns its index if it does
def find_rule(dsa_rules, in_rule):
    rv = None
    for dsa_rule in dsa_rules:
        dsa_rule_obj = vnc_read_obj(vnc, 'dsa-rule', dsa_rule['to'])
        entry = dsa_rule_obj.get_dsa_rule_entry()
        if match_rule_entry(entry, in_rule):
            rv = dsa_rule_obj
    return rv
# end

def print_dsa_rule_entry(entry, prefix = ''):
    pub = entry.get_publisher()
    sub = entry.get_subscriber()[0]
    pub_str = '%s/%d,%s,%s,%s' % \
        (pub.ep_prefix.ip_prefix, pub.ep_prefix.ip_prefix_len,
        pub.ep_type, pub.ep_id, pub.ep_version)
    sub_str = '%s/%d,%s,%s,%s' % \
        (sub.ep_prefix.ip_prefix, sub.ep_prefix.ip_prefix_len,
        sub.ep_type, sub.ep_id, sub.ep_version)
    print '%s %s %s' % (prefix, pub_str, sub_str)

"""
[
  {
    u'to': [u'default-discovery-service-assignment', u'default-dsa-rule'],
    u'href': u'http://127.0.0.1:8082/dsa-rule/b241e9e7-2085-4a8b-8e4b-375ebf4a6dba',
    u'uuid': u'b241e9e7-2085-4a8b-8e4b-375ebf4a6dba'
  }
]
"""
def show_dsa_rules(vnc, dsa_rules):
    if dsa_rules is None:
        print 'Empty DSA group!'
        return

    print 'Rules (%d):' % len(dsa_rules)
    print '----------'
    idx = 1
    for rule in dsa_rules:
            dsa_rule = vnc_read_obj(vnc, 'dsa-rule', rule['to'])
            entry = dsa_rule.get_dsa_rule_entry()
            # entry is empty by default in a DSA rule object
            if entry:
                print_dsa_rule_entry(entry, prefix = '%d)' % idx)
            idx += 1
    print ''
# end

def vnc_read_obj(vnc, obj_type, fq_name):
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        return method(fq_name=fq_name)
    except NoIdError:
        print '%s %s not found!' % (obj_type, fq_name)
        return None
# end

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--server', help="Discovery server address in the form IP:Port",
        default = '127.0.0.1:5998')
    parser.add_argument(
        '--api-server', help="API server address in the form IP:Port",
        default = '127.0.0.1:8082')
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
    valid_ops = ['read', 'add-rule', 'del-rule', 'create', 'delete', 'load-balance']
    parser.add_argument(
        '--op', choices = valid_ops, help="Operation to perform")
    parser.add_argument(
        '--name', help="FQN of discovery-service-assignment object",
        default = 'default-discovery-service-assignment')
    parser.add_argument('--rule',  help="Rule to add or delete")
    parser.add_argument('--uuid', help="object UUID")
    parser.add_argument(
        '--os-username',  help="Keystone User Name", default=None)
    parser.add_argument(
        '--os-password',  help="Keystone User Password", default=None)
    parser.add_argument(
        '--os-tenant-name',  help="Keystone Tenant Name", default=None)

    args = parser.parse_args()
    return args
# end parse_args

def get_ks_var(args, name):
    opts = vars(args)

    uname = name.upper()
    cname = '-'.join(name.split('_'))
    if opts['os_%s' % (name)]:
        value = opts['os_%s' % (name)]
        return (value, '')

    rsp = ''
    try:
        value = os.environ['OS_' + uname]
        if value == '':
            value = None
    except KeyError:
        value = None

    if value is None:
        rsp = 'You must provide a %s via either --os-%s or env[OS_%s]' % (
            name, cname, uname)
    return (value, rsp)
# end

args = parse_args()

# Validate Discovery server information
server = args.server.split(':')
if len(server) != 2:
    print 'Discovery server address must be of the form ip:port, '\
          'for example 127.0.0.1:5998'
    sys.exit(1)
server_ip = server[0]
server_port = server[1]


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
    sys.exit(0)
elif args.load_balance or args.op == 'load-balance':
    if not args.service_type:
        print 'Please specify service type'
        sys.exit(1)
    if args.service_id:
        print 'Specific service id %s ignored for this operation' % args.service_id
    url = "http://%s:%s/load-balance/%s" % (server_ip, server_port, args.service_type)
    r = requests.post(url)
    if r.status_code != 200:
        print "Operation status %d" % r.status_code
    sys.exit(0)

# Validate API server information
api_server = args.api_server.split(':')
if len(api_server) != 2:
    print 'Discovery server address must be of the form ip:port, '\
          'for example 127.0.0.1:5998'
    sys.exit(1)
api_server_ip = api_server[0]
api_server_port = api_server[1]

# Validate keystone credentials
conf = {}
for name in ['username', 'password', 'tenant_name']:
    val, rsp = get_ks_var(args, name)
    if val is None:
        print rsp
        sys.exit(1)
    conf[name] = val

username = conf['username']
password = conf['password']
tenant_name = conf['tenant_name']

vnc = VncApi(username, password, tenant_name,
             api_server[0], api_server[1])

uuid = args.uuid
# transform uuid if needed
if uuid and '-' not in uuid:
    uuid = str(__uuid.UUID(uuid))
fq_name = vnc.id_to_fq_name(uuid) if uuid else args.name.split(':')

print ''
print 'Oper       = ', args.op
print 'Name       = %s' % fq_name
print 'UUID       = %s' % uuid
print 'API Server = ', args.server
print 'Discovery Server = ', args.server
print ''

if args.op == 'add-rule':
    if not args.rule:
        print 'Error: missing rule'
        sys.exit(1)
    rule_entry = build_dsa_rule_entry(args.rule)
    if rule_entry is None:
        show_usage()
        sys.exit(1)

    # name is of discovery-service-assignment object
    # which consists of one or more rules
    dsa = vnc.discovery_service_assignment_read(fq_name = fq_name)
    dsa_rules = dsa.get_dsa_rules()
    show_dsa_rules(vnc, dsa_rules)

    print ''
    print_dsa_rule_entry(rule_entry)

    ans = raw_input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    rule_uuid = __uuid.uuid4()
    dsa_rule = DsaRule(name = str(rule_uuid), parent_obj = dsa, dsa_rule_entry = rule_entry)
    dsa_rule.set_uuid(str(rule_uuid))
    vnc.dsa_rule_create(dsa_rule)
elif args.op == 'read':
    dsa = vnc_read_obj(vnc, 'discovery-service-assignment', fq_name)
    if dsa == None:
        sys.exit(1)

    dsa_rules = dsa.get_dsa_rules()
    show_dsa_rules(vnc, dsa_rules)
elif args.op == 'del-rule':
    if args.rule is None:
        print 'Error: missing rule'
        sys.exit(1)
    rule = build_dsa_rule_entry(args.rule)
    if rule is None:
        show_usage()
        sys.exit(1)

    dsa = vnc.discovery_service_assignment_read(fq_name = fq_name)
    dsa_rules = dsa.get_dsa_rules()
    if dsa_rules is None:
        print 'Empty DSA group!'
        sys.exit(1)
    show_dsa_rules(vnc, dsa_rules)

    obj = find_rule(dsa_rules, rule)
    if not obj:
        print 'Rule not found. Unchanged'
        sys.exit(1)
    else:
        print 'Rule found!'

    ans = raw_input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    vnc.dsa_rule_delete(id = obj.uuid)
