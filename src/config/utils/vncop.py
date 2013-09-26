#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import argparse
import uuid
import os

from vnc_api.vnc_api import *
from vnc_api.gen.resource_common import *


def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name
# end CamelCase


def str_to_class(class_name):
    return reduce(getattr, class_name.split("."), sys.modules[__name__])


class VncOp():

    def parse_args(self):
        # Eg. python vnc_op.py VirtualNetwork
        # domain:default-project:default-virtual-network

        parser = argparse.ArgumentParser(description="Create empty object")
        parser.add_argument(
            'server', help="API server address in the form ip:port")
        parser.add_argument(
            'oper', help="Operation eg. read, write, create, delete")
        parser.add_argument('--type', help="object type eg. virtual-network")
        parser.add_argument(
            '--name', help="colon seperated fully qualified name")
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

print 'Oper = ', vnc_op.args.oper
print 'Type = ', vnc_op.args.type
print 'Name = ', vnc_op.args.name
print 'API Server = ', vnc_op.args.server

ui = {}
if vnc_op.args.user:
    ui['user'] = vnc_op.args.user
if vnc_op.args.role:
    ui['role'] = vnc_op.args.role
if ui:
    print 'Sending user, role as %s/%s' % (vnc_op.args.user, vnc_op.args.role)

vnc = VncApi(username, password, tenant_name,
             server[0], server[1], user_info=ui)

if vnc_op.args.uuid:
    print 'UUID = ', vnc_op.args.uuid
    fq_name = vnc.id_to_fq_name(vnc_op.args.uuid)
    print 'FQNAME = ', fq_name
    sys.exit(1)

domain = Domain(name='default-domain')
project = Project(name='admin')
ipam = NetworkIpam(project_obj=project)
vns = VnSubnetsType()

# create object
fq_name = vnc_op.args.name.split(':')
cls = str_to_class(CamelCase(vnc_op.args.type))
obj = cls(name=fq_name[-1])

if vnc_op.args.oper == 'create':
    if len(fq_name) >= 1:
        obj = domain = Domain(name=fq_name[0])
    if len(fq_name) >= 2:
        obj = project = Project(name=fq_name[1], domain_obj=domain)
    if len(fq_name) >= 3:
        obj = network = VirtualNetwork(
            name=fq_name[2], project_obj=project)
    method_name = vnc_op.args.type.replace('-', '_')
    method = getattr(vnc, "%s_create" % (method_name))
    #obj.set_network_ipam(ipam, vns)
    id = method(obj)
    print 'Created %s "%s", uuid = %s' % (vnc_op.args.type,
                                          vnc_op.args.name, id)
    obj.dump()
elif vnc_op.args.oper == 'read':
    method_name = vnc_op.args.type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    obj = method(fq_name=fq_name)
    print 'Read %s "%s"' % (vnc_op.args.type, fq_name)
    obj.dump()
elif vnc_op.args.oper == 'update':
    # read
    method_name = vnc_op.args.type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    obj = method(fq_name=fq_name)
    print 'Read %s "%s"' % (vnc_op.args.type, fq_name)
    obj.dump()
    # print ''

    # write
    method_name = vnc_op.args.type.replace('-', '_')
    method = getattr(vnc, "%s_update" % (method_name))
    result = method(obj)
    print 'Write result = ', result
elif vnc_op.args.oper == 'delete':
    method_name = vnc_op.args.type.replace('-', '_')
    method = getattr(vnc, "%s_delete" % (method_name))
    result = method(fq_name=fq_name)
    print 'Deleting %s "%s"' % (vnc_op.args.type, fq_name)
    print 'Result = ', result
