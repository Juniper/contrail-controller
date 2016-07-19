#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Util to manage RBAC group and rules (add, delete etc)",
#
import argparse
import uuid as __uuid
import os
import re

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import *
from cfgm_common.exceptions import *

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
        print 'Empty RBAC group!'
        return

    # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
    rule_list = api_access_list_entries.get_rbac_rule()
    print 'Rules (%d):' % len(rule_list)
    print '----------'
    idx = 1
    for rule in rule_list:
            o = rule.rule_object
            f = rule.rule_field
            ps = ''
            for p in rule.rule_perms:
                ps += p.role_name + ':' + p.role_crud + ','
            o_f = "%s.%s" % (o,f) if f else o
            print '%2d %-32s   %s' % (idx, o_f, ps)
            idx += 1
    print ''
# end

# match two rules (type RbacRuleType)
# r1 is operational, r2 is part of rbac group
# return (obj_type & Field match, rule is subset of existing rule, match index, merged rule
def match_rule(r1, r2):
    if r1.rule_object != r2.rule_object:
        return None
    if r1.rule_field != r2.rule_field:
        return None

    s1 = set(r.role_name+":"+r.role_crud for r in r1.rule_perms)
    s2 = set(r.role_name+":"+r.role_crud for r in r2.rule_perms)

    d1 = {r.role_name:set(list(r.role_crud)) for r in r1.rule_perms}
    d2 = {r.role_name:set(list(r.role_crud)) for r in r2.rule_perms}

    diffs = {}
    for role, cruds in d2.items():
        diffs[role] = cruds - d1.get(role, set([]))
    diffs = {role:crud for role,crud in diffs.items() if len(crud) != 0}

    merge = d2.copy()
    for role, cruds in d1.items():
        merge[role] = cruds|d2.get(role, set([]))

    return [True, s1==s2, diffs, merge]
# end

# check if rule already exists in rule list and returns its index if it does
def find_rule(rge, rule):
    idx = 1
    for r in rge.rbac_rule:
        m = match_rule(rule, r)
        if m:
            m[0] = idx
            return m
        idx += 1
    return None
# end

def build_perms(rule, perm_dict):
    rule.rule_perms = []
    for role_name, role_crud in perm_dict.items():
        rule.rule_perms.append(RbacPermType(role_name, "".join(role_crud)))
# end

# build rule object from string form
# "useragent-kv *:CRUD" (Allow all operation on /useragent-kv API)
def build_rule(rule_str):
    r = rule_str.split(" ", 1) if rule_str else []
    if len(r) < 2:
        return None

    # [0] is object.field, [1] is list of perms
    obj_field = r[0].split(".")
    perms = r[1].split(",")

    o = obj_field[0]
    f = obj_field[1] if len(obj_field) > 1 else None
    o_f = "%s.%s" % (o,f) if f else o
    print 'rule: %s   %s' % (o_f, r[1])

    # perms eg ['foo:CRU', 'bar:CR']
    rule_perms = []
    for perm in perms:
        p = perm.strip().split(":")
        rule_perms.append(RbacPermType(role_name = p[0], role_crud = p[1]))

    # build rule
    rule = RbacRuleType(
              rule_object = o,
              rule_field = f,
              rule_perms = rule_perms)
    return rule
#end

# Read VNC object. Return None if object doesn't exists
def vnc_read_obj(vnc, obj_type, fq_name):
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        return method(fq_name=fq_name)
    except NoIdError:
        print '%s %s not found!' % (obj_type, fq_name)
        return None
# end

class VncRbac():

    def parse_args(self):
        # Eg. python vnc_op.py VirtualNetwork
        # domain:default-project:default-virtual-network

        defaults = {
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
            '--on',  help="Enable RBAC", action="store_true")
        parser.add_argument(
            '--off',  help="Disable RBAC", action="store_true")
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
    print 'API server address must be of the form ip:port, '\
          'for example 127.0.0.1:8082'
    sys.exit(1)


# Validate keystone credentials
conf = {}
for name in ['username', 'password', 'tenant_name']:
    val, rsp = vnc_op.get_ks_var(name)
    if val is None:
        print rsp
        sys.exit(1)
    conf[name] = val

username = conf['username']
password = conf['password']
tenant_name = conf['tenant_name']
obj_type = 'api-access-list'

if vnc_op.args.on and vnc_op.args.off:
    print 'Only one of --on or --off must be specified'
    sys.exit(1)

ui = {}
if vnc_op.args.user:
    ui['user'] = vnc_op.args.user
if vnc_op.args.role:
    ui['role'] = vnc_op.args.role
if ui:
    print 'Sending user, role as %s/%s' % (vnc_op.args.user, vnc_op.args.role)

vnc = VncApi(username, password, tenant_name,
             server[0], server[1], user_info=ui)

url = '/multi-tenancy-with-rbac'
if vnc_op.args.on or vnc_op.args.off:
    data = {'enabled': vnc_op.args.on}
    try:
        rv = vnc._request_server(rest.OP_PUT, url, json.dumps(data))
    except PermissionDenied:
        print 'Permission denied'
        sys.exit(1)
elif vnc_op.args.uuid and vnc_op.args.name:
    print 'Only one of uuid and fqname should be specified'
    sys.exit(1)

try:
    rv_json = vnc._request_server(rest.OP_GET, url)
    rv = json.loads(rv_json)
    print 'Rbac is %s' % ('enabled' if rv['enabled'] else 'disabled')
except Exception as e:
    print str(e)
    print 'Rbac not supported'
    sys.exit(1)

if not vnc_op.args.uuid and not vnc_op.args.name:
    sys.exit(1)


uuid = vnc_op.args.uuid
# transform uuid if needed
if uuid and '-' not in uuid:
    uuid = str(__uuid.UUID(uuid))

fq_name = vnc.id_to_fq_name(uuid) if uuid else vnc_op.args.name.split(':')

print ''
print 'Oper       = ', vnc_op.args.op
print 'Name       = %s' % fq_name
print 'UUID       = %s' % uuid
print 'API Server = ', vnc_op.args.server
print ''

if vnc_op.args.op == 'create':
    # given uuid of domain or parent
    if vnc_op.args.uuid:
        fq_name.append('default-api-access-list')
    elif len(fq_name) != 2 and len(fq_name) != 3:
        print 'Fully qualified name of rbac group expected'
        print '<domain>:<rback-group> or <domain>:<project>:<api-access-list>'
        sys.exit(1)

    name = fq_name[-1]

    if len(fq_name) == 2:
       pobj = vnc.domain_read(fq_name = fq_name[0:1])
    else:
       pobj = vnc.project_read(fq_name = fq_name[0:2])

    ans = raw_input("Create %s, confirm (y/n): " % fq_name)
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
        print 'Fully qualified name of rbac group expected'
        print '<domain>:<rback-group> or <domain>:<project>:<api-access-list>'
        sys.exit(1)
    name = fq_name[-1]

    rg = vnc_read_obj(vnc, 'api-access-list', fq_name)
    if rg == None:
        sys.exit(1)
    rge = rg.get_api_access_list_entries()
    show_rbac_rules(rge)

    ans = raw_input("Confirm (y/n): ")
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
        print 'A rule string must be specified for this operation'
        print 'rule format: <resource>.<field> role1:<crud1>,role2:<crud2>'
        print 'eg * admin:CRUD'
        print 'eg virtual-network admin:CRUD'
        print 'eg virtual-network.subnet admin:CRUD,member:R'
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

    ans = raw_input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    rg.set_api_access_list_entries(rge)
    vnc.api_access_list_update(rg)
elif vnc_op.args.op == 'del-rule':
    if vnc_op.args.rule is None:
        print 'A rule string must be specified for this operation'
        print 'Rule format: <resource>.<field> role1:<crud1>,role2:<crud2>'
        print 'eg virtual-network admin:CRUD'
        print 'eg virtual-network.subnet admin:CRUD,member:R'
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
            print 'Invalid rule index to delete. Value must be 1-%d' % rc
            sys.exit(1)
        match = (del_idx, True)
    else:
        rule = build_rule(vnc_op.args.rule)
        match = find_rule(rge, rule)

    if not match:
        print 'Rule not found. Unchanged'
        sys.exit(1)
    elif match[1]:
        rge.rbac_rule.pop(match[0]-1)
    else:
        build_perms(rge.rbac_rule[match[0]-1], match[2])
    show_rbac_rules(rge)

    ans = raw_input("Confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)

    rg.set_api_access_list_entries(rge)
    vnc.api_access_list_update(rg)
