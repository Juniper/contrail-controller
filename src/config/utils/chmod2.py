#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import argparse
import uuid as __uuid
import os

from vnc_api.vnc_api import *


class VncChmod():

    def parse_args(self):
        # Eg. python chmod.py VirtualNetwork
        # domain:default-project:default-virtual-network

        parser = argparse.ArgumentParser(
            description="Show and change object permissions and ownership")
        parser.add_argument(
            'server', help="API server address in the form ip:port")
        parser.add_argument('--type', help="object type eg. virtual-network")
        parser.add_argument(
            '--name',
            help="colon seperated fully qualified name, eg "
            "default-domain:default-project:default-virtual-network")
        parser.add_argument(
            '--uuid',
            help="Object UUID eg f1401b7c-e387-4eec-a46c-230dfd695fae")
        parser.add_argument('--user',  help="User Name")
        parser.add_argument('--role',  help="Role Name")
        parser.add_argument('--owner', help="Set owner tenant")
        parser.add_argument('--owner-access', type=int, help="Set owner access")
        valid_ops = ['on', 'off']
        parser.add_argument('--globally-shared', choices = valid_ops,
            help="Enable or Disable globally disabled flag ")
        parser.add_argument('--share-list', help="Set share list")
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
# end

def print_perms(obj_perms):
    share_perms = ['%s:%d' % (x.tenant, x.tenant_access) for x in obj_perms.permissions.share]
    return '%s/%d %d %s' \
        % (obj_perms.permissions.owner, obj_perms.permissions.owner_access,
           obj_perms.permissions.globally_shared, share_perms)
# end print_perms

def set_perms(obj, owner=None, owner_access=None, share=None, globally_shared=None):
    perms = obj.get_id_perms2()
    print 'Current perms %s = %s' % (obj.get_fq_name(), print_perms(perms))

    if owner:
        perms.permissions.owner = owner

    if owner_access:
        perms.permissions.owner_access = owner_access

    if share is not None:
        perms.permissions.share = [ShareType(obj_uuid, obj_crud) for (obj_uuid, obj_crud) in share]

    if globally_shared is not None:
        perms.permissions.globally_shared = globally_shared

    obj.set_id_perms2(perms)
    print 'New perms %s = %s' % (obj.get_fq_name(), print_perms(perms))
# end set_perms

chmod = VncChmod()
chmod.parse_args()
conf = {}
args = chmod.args

# Validate API server information
server = chmod.args.server.split(':')
if len(server) != 2:
    print 'API server address must be of the form ip:port,'\
          'for example 127.0.0.1:8082'
    sys.exit(1)
if chmod.args.uuid:
    if chmod.args.type or chmod.args.name:
        print 'Ignoring type/name; will derive from UUID'
elif not chmod.args.type:
    print 'You must provide a object type via --type'
    sys.exit(1)
elif not chmod.args.name:
    print 'You must provide a fully qualified name via --name'
    sys.exit(1)

# Validate keystone credentials
for name in ['username', 'password', 'tenant_name']:
    val, rsp = chmod.get_ks_var(name)
    if val is None:
        print rsp
        sys.exit(1)
    conf[name] = val

print 'API Server = ', chmod.args.server
if chmod.args.owner:
    print 'Owner = ', chmod.args.owner
if chmod.args.owner_access:
    print 'Owner access = ', chmod.args.owner_access
if chmod.args.globally_shared:
    print 'globally_shared = ', chmod.args.globally_shared
if chmod.args.share_list:
    print 'share_list = ', chmod.args.share_list

ui = {}
if chmod.args.user:
    ui['user'] = chmod.args.user
if chmod.args.role:
    ui['role'] = chmod.args.role
if ui:
    print 'Sending user, role as %s/%s' % (chmod.args.user, chmod.args.role)

print 'Keystone credentials %s/%s/%s' % (conf['username'],
                                         conf['password'],
                                         conf['tenant_name'])
vnc = VncApi(conf['username'], conf['password'], conf[
             'tenant_name'], server[0], server[1], user_info=ui)

if chmod.args.uuid:
    if '-' not in chmod.args.uuid:
        chmod.args.uuid = str(__uuid.UUID(chmod.args.uuid))
    name, type = vnc.id_to_fq_name_type(chmod.args.uuid)
    chmod.args.type = type
    chmod.args.name = ":".join(name)

print 'Type = ', chmod.args.type
print 'Name = ', chmod.args.name

# read object from API server
method_name = chmod.args.type.replace('-', '_')
method = getattr(vnc, "%s_read" % (method_name))
obj = method(fq_name_str=chmod.args.name)
print 'Cur perms %s' % print_perms(obj.get_id_perms2())

# write to API server
if args.owner or args.owner_access or args.globally_shared or args.share_list:
    share_list = None
    if args.share_list:
       share_list = []
       "tenantA:rwx, tenantB:rwx, ...."
       shares = args.share_list.split(",")
       for item in shares:
           x = item.split(":")
           try:
               share_list.append((x[0], int(x[1])))
           except ValueError:
               print 'share list is tuple of <uuid:octal-perms>, for example "0ed5ea...700:7"'
               sys.exit(1)
    set_perms(obj,
        owner = chmod.args.owner,
        owner_access = chmod.args.owner_access,
        globally_shared = True if chmod.args.globally_shared == 'on' else False,
        share = share_list)
    print 'New perms %s' % print_perms(obj.get_id_perms2())
    ans = raw_input("Update perms? confirm (y/n): ")
    if not ans or ans[0].lower() != 'y':
        sys.exit(0)
    obj.set_id_perms2(obj.get_id_perms2())
    write_method = getattr(vnc, "%s_update" % (method_name))
    rv = write_method(obj)
