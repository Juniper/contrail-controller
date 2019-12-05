from __future__ import print_function
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Create, read, update and delete an object
#
from builtins import str
from builtins import object
import argparse
import uuid as __uuid
import os

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import *
from cfgm_common.exceptions import *
from functools import reduce


def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name
# end CamelCase


def str_to_class(class_name):
    return reduce(getattr, class_name.split("."), sys.modules[__name__])

def print_perms2(obj):
    perms2 = obj.get_perms2()
    if perms2 is None:
        print('** EMPTY PERMS2 **')
        return

    share_perms = ['%s:%s' % (x.tenant, x.tenant_access) for x in perms2.share]
    print('perms2 o=%s/%d g=%d s=%s' \
        % (perms2.owner, perms2.owner_access,
           perms2.global_access, share_perms))
# end print_perms

# Read VNC object. Return None if object doesn't exists
def vnc_read_obj(vnc, obj_type, fq_name):
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        return method(fq_name=fq_name)
    except NoIdError:
        print('%s %s not found!' % (obj_type, fq_name))
        return None
    except PermissionDenied:
        print('No permission to read %s' % fq_name)
        return None
# end

class VncOp(object):

    def parse_args(self):
        # Eg. python vnc_op.py VirtualNetwork
        # domain:default-project:default-virtual-network

        parser = argparse.ArgumentParser(description="Create empty object")
        parser.add_argument(
            '--server', help="API server address in the form IP:Port",
            default = '127.0.0.1:8082')
        valid_ops = ['read', 'write', 'create', 'delete']
        parser.add_argument(
            '--op', choices = valid_ops, help="Operation to perform")
        parser.add_argument('--type', help="object type eg. virtual-network")
        parser.add_argument(
            '--name', help="colon seperated fully qualified name of object or parent")
        parser.add_argument(
            '--collection',  help="Enable collection", action="store_true")
        parser.add_argument('--uuid', help="object UUID")
        parser.add_argument('--user',  help="User Name")
        parser.add_argument('--role',  help="Role Name")
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

vnc_op = VncOp()
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

print('Oper = ', vnc_op.args.op)
print('API Server = ', vnc_op.args.server)

ui = {}
if vnc_op.args.user:
    ui['user'] = vnc_op.args.user
if vnc_op.args.role:
    ui['role'] = vnc_op.args.role
if ui:
    print('Sending user, role as %s/%s' % (vnc_op.args.user, vnc_op.args.role))

vnc = VncApi(username, password, tenant_name,
             server[0], server[1], user_info=ui)

if vnc_op.args.uuid and vnc_op.args.name:
    print('Only one of uuid and fqname should be specified')
    sys.exit(1)
elif not vnc_op.args.uuid and not vnc_op.args.name:
    print('One of uuid or fqname should be specified')
    sys.exit(1)

uuid = vnc_op.args.uuid
if uuid:
    if '-' not in uuid:
        uuid = str(__uuid.UUID(uuid))
    print('UUID = ', uuid)
    fq_name, obj_type = vnc.id_to_fq_name_type(uuid)
    if vnc_op.args.type and obj_type != vnc_op.args.type:
        print('Object type mismatch! From UUID %s, specified %s' %\
            (obj_type, vnc_op.args.type))
        sys.exit(1)
else:
    fq_name = vnc_op.args.name.split(':')
    obj_type = vnc_op.args.type

print('Name = ', fq_name)
print('Type = ', obj_type)

if obj_type is None:
    print('Missing object type')
    sys.exit(1)

cls = str_to_class(CamelCase(obj_type))

if vnc_op.args.op == 'create':
    if obj_type == 'api-access-list':
        ptype = 'domain' if len(fq_name) == 2 else 'project'
        obj = cls(name=fq_name[-1], parent_type = ptype)
    else:
        obj = cls(name=fq_name[-1])

    if len(fq_name) >= 1:
        obj = domain = Domain(name=fq_name[0])
    if len(fq_name) >= 2:
        obj = project = Project(name=fq_name[1], domain_obj=domain)
    if len(fq_name) >= 3:
        obj = network = VirtualNetwork(
            name=fq_name[2], parent_obj=project)
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_create" % (method_name))
    #obj.set_network_ipam(ipam, vns)
    id = method(obj)
    print('Created %s "%s", uuid = %s' % (obj_type,
                                          vnc_op.args.name, id))
    obj2 = vnc_read_obj(vnc, obj_type, obj.get_fq_name())
    obj2.dump()
    print_perms2(obj2)
elif vnc_op.args.op == 'read':
    if vnc_op.args.collection:
        method_name = obj_type.replace('-', '_')
        method = getattr(vnc, "%ss_list" % (method_name))
        coll_info = method(parent_fq_name=fq_name)
        fq_name_list = [item['fq_name'] for item in coll_info["%ss" % obj_type]]
        for fq_name in fq_name_list:
            obj = vnc_read_obj(vnc, obj_type, fq_name)
            if obj:
                obj.dump()
                print_perms2(obj)
    else:
        obj = vnc_read_obj(vnc, obj_type, fq_name)
        if obj:
            print('Read %s "%s"' % (obj_type, fq_name))
            obj.dump()
            print_perms2(obj)
elif vnc_op.args.op == 'update':
    # read
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    obj = method(fq_name=fq_name)
    print('Read %s "%s"' % (obj_type, fq_name))
    obj.dump()
    # print ''

    # write
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_update" % (method_name))
    result = method(obj)
    print('Write result = ', result)
elif vnc_op.args.op == 'delete':
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_delete" % (method_name))
    result = method(fq_name=fq_name)
    print('Deleting %s "%s"' % (obj_type, fq_name))
    print('Result = ', result)
