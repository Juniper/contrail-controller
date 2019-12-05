from __future__ import print_function
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Util to manage RBAC group and rules (add, delete etc)",
#
from builtins import input
from builtins import str
from builtins import object
import argparse
import uuid as __uuid
import os
import re
import sys

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import *
from cfgm_common.exceptions import *
from cfgm_common.rbaclib import *
import cfgm_common
from vnc_api.utils import AAA_MODE_VALID_VALUES

example_usage = \
"""
 Examples:
 ---------
 Read RBAC group using UUID or FQN
 python rbacutil.py --uuid 'b27c3820-1d5f-4bfd-ba8b-246fefef56b0' --op read
 python rbacutil.py --name 'default-domain:default-api-access-list' --op read

 Create RBAC group using FQN or UUID or parent domain/project
 python rbacutil.py --uuid 696de19995ff4359882afd18bb6dfecf --op create
 python rbacutil.py --fq_name 'default-domain:my-api-access-list' --op create

 Delete RBAC group using FQN or UUID
 python rbacutil.py --name 'default-domain:foobar' --op delete
 python rbacutil.py --uuid 71ef050f-3487-47e1-b628-8b0949530bee --op delete

 Add rule to existing RBAC group
 python rbacutil.py --uuid <uuid> --rule "* Member:R" --op add-rule
 python rbacutil.py --uuid <uuid> --rule "useragent-kv *:CRUD" --op add-rule

 Delete rule from RBAC group - specify rule number or exact rule
 python rbacutil.py --uuid <uuid> --rule 2 --op del-rule
 python rbacutil.py --uuid <uuid> --rule "useragent-kv *:CRUD" --op del-rule
"""

def show_rbac_rules(api_access_list_entries):
    if api_access_list_entries is None:
        print('Empty RBAC group!')
        return

    # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
    rule_list = api_access_list_entries.get_rbac_rule()
    print('Rules (%d):' % len(rule_list))
    print('----------')
    idx = 1
    for rule in rule_list:
            o = rule.rule_object
            f = rule.rule_field
            ps = ''
            for p in rule.rule_perms:
                ps += p.role_name + ':' + p.role_crud + ','
            o_f = "%s.%s" % (o,f) if f else o
            print('%2d %-32s   %s' % (idx, o_f, ps))
            idx += 1
    print('')
# end

# Read VNC object. Return None if object doesn't exists
def vnc_read_obj(vnc, obj_type, fq_name):
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        return method(fq_name=fq_name)
    except NoIdError:
        print('%s %s not found!' % (obj_type, fq_name))
        return None
# end

class VncRbac(object):

    def parse_args(self):
        # Eg. python vnc_op.py VirtualNetwork
        # domain:default-project:default-virtual-network

        defaults = {
            'aaa_mode': None,
            'name': 'default-global-system-config:default-api-access-list'
        }

        parser = argparse.ArgumentParser(
            description="Util to manage RBAC group and rules",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog = example_usage)
        parser.add_argument(
            '--server', help="API server address in the form IP:Port",
            default = '127.0.0.1:8082')
        valid_ops = ['read', 'add-rule', 'del-rule', 'create', 'delete']
        parser.add_argument(
            '--op', choices = valid_ops, help="Operation to perform")
        parser.add_argument(
            '--name', help="colon seperated fully qualified name",
            default='default-global-system-config:default-api-access-list')
        parser.add_argument('--uuid', help="object UUID")
        parser.add_argument('--user',  help="User Name")
        parser.add_argument('--role',  help="Role Name")
        parser.add_argument('--rule',  help="Rule to add or delete")
        parser.add_argument(
            '--aaa_mode', choices = AAA_MODE_VALID_VALUES, help="AAA mode")
        parser.add_argument(
            '--os-username',  help="Keystone User Name", default=None)
        parser.add_argument(
            '--os-password',  help="Keystone User Password", default=None)
        parser.add_argument(
            '--os-tenant-name',  help="Keystone Tenant Name", default=None)

        self.args = parser.parse_args()
        self.opts = vars(self.args)
    # end parse_args

    def get_ks_var(self, name):
        uname = name.upper()
        cname = '-'.join(name.split('_'))
        if self.opts['os_%s' % (name)]:
            value = self.opts['os_%s' % (name)]
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

vnc_op = VncRbac()
vnc_op.parse_args()

# Validate API server information
server = vnc_op.args.server.split(':')
if len(server) != 2:
    print('API server address must be of the form ip:port, '\
          'for example 127.0.0.1:8082')
    sys.exit(1)


# Validate keystone credentials
conf = {}
for name in ['username', 'password', 'tenant_name']:
    val, rsp = vnc_op.get_ks_var(name)
    if val is None:
        print(rsp)
        sys.exit(1)
    conf[name] = val

username = conf['username']
password = conf['password']
tenant_name = conf['tenant_name']
obj_type = 'api-access-list'

ui = {}
if vnc_op.args.user:
    ui['user'] = vnc_op.args.user
if vnc_op.args.role:
    ui['role'] = vnc_op.args.role
if ui:
    print('Sending user, role as %s/%s' % (vnc_op.args.user, vnc_op.args.role))

vnc = VncApi(username, password, tenant_name,
             server[0], server[1], user_info=ui)

if vnc_op.args.aaa_mode:
    try:
        rv = vnc.set_aaa_mode(vnc_op.args.aaa_mode)
    except PermissionDenied:
        print('Permission denied')
        sys.exit(1)

try:
    rv = vnc.get_aaa_mode()
    print('AAA mode is %s' % rv['aaa-mode'])
except Exception as e:
    print(str(e))
    print('Rbac not supported')
    sys.exit(1)

if not vnc_op.args.uuid and not vnc_op.args.name:
    sys.exit(1)


uuid = vnc_op.args.uuid
# transform uuid if needed
if uuid and '-' not in uuid:
    uuid = str(__uuid.UUID(uuid))

fq_name = vnc.id_to_fq_name(uuid) if uuid else vnc_op.args.name.split(':')

print('')
print('Oper       = ', vnc_op.args.op)
print('Name       = %s' % fq_name)
print('UUID       = %s' % uuid)
print('API Server = ', vnc_op.args.server)
print('')

if vnc_op.args.op == 'create':
    # given uuid of domain or parent
    if vnc_op.args.uuid:
        fq_name.append('default-api-access-list')
    elif len(fq_name) != 2 and len(fq_name) != 3:
        print('Fully qualified name of rbac group expected')
        print('<domain>:<rback-group> or <domain>:<project>:<api-access-list>')
        sys.exit(1)

    name = fq_name[-1]

    if len(fq_name) == 2:
       # could be in domain or global config
       if fq_name[0] == 'default-global-system-config':
           pobj = vnc.global_system_config_read(fq_name = fq_name[0:1])
       else:
           pobj = vnc.domain_read(fq_name = fq_name[0:1])
    else:
       pobj = vnc.project_read(fq_name = fq_name[0:2])

    ans = input("Create %s, confirm (y/n): " % fq_name)
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    #rentry = None
    #rule = RbacRuleType()
    #rentry = RbacRuleEntriesType([rule])
    rentry = RbacRuleEntriesType([])
    rg = ApiAccessList(name, parent_obj = pobj, api_access_list_entries = rentry)
    vnc.api_access_list_create(rg)

    rg2 = vnc.api_access_list_read(fq_name = fq_name)
    rge = rg.get_api_access_list_entries()
    show_rbac_rules(rge)
elif vnc_op.args.op == 'delete':
    if len(fq_name) != 2 and len(fq_name) != 3:
        print('Fully qualified name of rbac group expected')
        print('<domain>:<rback-group> or <domain>:<project>:<api-access-list>')
        sys.exit(1)
    name = fq_name[-1]

    rg = vnc_read_obj(vnc, 'api-access-list', fq_name)
    if rg == None:
        sys.exit(1)
    rge = rg.get_api_access_list_entries()
    show_rbac_rules(rge)

    ans = input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    vnc.api_access_list_delete(fq_name = fq_name)
elif vnc_op.args.op == 'read':
    rg = vnc_read_obj(vnc, 'api-access-list', fq_name)
    if rg == None:
        sys.exit(1)

    show_rbac_rules(rg.get_api_access_list_entries())
elif vnc_op.args.op == 'add-rule':
    rule = build_rule(vnc_op.args.rule)
    if rule is None:
        print('A rule string must be specified for this operation')
        print('rule format: <resource>.<field> role1:<crud1>,role2:<crud2>')
        print('eg * admin:CRUD')
        print('eg virtual-network admin:CRUD')
        print('eg virtual-network.subnet admin:CRUD,member:R')
        sys.exit(1)

    # rbac rule entry consists of one or more rules
    rg = vnc_read_obj(vnc, 'api-access-list', fq_name)
    if rg == None:
        sys.exit(1)

    rge = rg.get_api_access_list_entries()
    if rge is None:
        rge = RbacRuleEntriesType([])
    show_rbac_rules(rge)

    # avoid duplicates
    match = find_rule(rge, rule)
    if not match:
        rge.add_rbac_rule(rule)
    else:
        build_perms(rge.rbac_rule[match[0]-1], match[3])

    show_rbac_rules(rge)

    ans = input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    rg.set_api_access_list_entries(rge)
    vnc.api_access_list_update(rg)
elif vnc_op.args.op == 'del-rule':
    if vnc_op.args.rule is None:
        print('A rule string must be specified for this operation')
        print('Rule format: <resource>.<field> role1:<crud1>,role2:<crud2>')
        print('eg virtual-network admin:CRUD')
        print('eg virtual-network.subnet admin:CRUD,member:R')
        sys.exit(1)

    rg = vnc_read_obj(vnc, 'api-access-list', fq_name)
    if rg == None:
        sys.exit(1)
    rge = rg.get_api_access_list_entries()
    show_rbac_rules(rge)

    del_idx = re.match("^[0-9]+$", vnc_op.args.rule)
    if del_idx:
        del_idx = int(del_idx.group())
        rc = len(rge.rbac_rule)
        if del_idx > rc or del_idx < 1:
            print('Invalid rule index to delete. Value must be 1-%d' % rc)
            sys.exit(1)
        match = (del_idx, True)
    else:
        rule = build_rule(vnc_op.args.rule)
        match = find_rule(rge, rule)

    if not match:
        print('Rule not found. Unchanged')
        sys.exit(1)
    elif match[1]:
        rge.rbac_rule.pop(match[0]-1)
    else:
        build_perms(rge.rbac_rule[match[0]-1], match[2])
    show_rbac_rules(rge)

    ans = input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    rg.set_api_access_list_entries(rge)
    vnc.api_access_list_update(rg)
