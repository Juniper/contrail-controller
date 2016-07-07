#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.

# Show and change object ownership and permissions2 fields
#
import argparse
import uuid as __uuid
import os
import cfgm_common.exceptions

from vnc_api.vnc_api import *


class VncChmod():

    def parse_args(self):
        # Eg. python chmod.py VirtualNetwork
        # domain:default-project:default-virtual-network

        parser = argparse.ArgumentParser(
            description="Show and change object permissions2 and ownership")
        parser.add_argument(
            '--server', help="API server address in the form ip:port",
            default = '127.0.0.1:8082')
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
        parser.add_argument('--owner-access', help="Set owner access")
        parser.add_argument('--global-access', help="Set global access")
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
    share_perms = ['%s:%d' % (x.tenant, x.tenant_access) for x in obj_perms.share]
    return '%s/%d %d %s' \
        % (obj_perms.owner, obj_perms.owner_access,
           obj_perms.global_access, share_perms)
# end print_perms

def set_perms(obj, owner=None, owner_access=None, share=None, global_access=None):
    global vnc
    try:
        rv = vnc.chmod(obj.get_uuid(), owner, owner_access, share, global_access)
        if rv == None:
            print 'Error in setting perms'
    except cfgm_common.exceptions.PermissionDenied:
        print 'Permission denied!'
        sys.exit(1)
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
if chmod.args.global_access:
    print 'global_access = ', chmod.args.global_access
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
    try:
        name, type = vnc.id_to_fq_name_type(chmod.args.uuid)
    except cfgm_common.exceptions.NoIdError:
        print 'Unknown UUID %s' % chmod.args.uuid
        sys.exit(1)
    except cfgm_common.exceptions.PermissionDenied:
        print 'Permission denied!'
        sys.exit(1)
    chmod.args.type = type
    chmod.args.name = ":".join(name)

print 'Type = ', chmod.args.type
print 'Name = ', chmod.args.name

# read object from API server
method_name = chmod.args.type.replace('-', '_')
method = getattr(vnc, "%s_read" % (method_name))
try:
    obj = method(fq_name_str=chmod.args.name)
    print 'Cur perms %s' % print_perms(obj.get_perms2())
except cfgm_common.exceptions.PermissionDenied:
    print 'Permission denied!'
    sys.exit(1)

# write to API server
if args.owner or args.owner_access or args.global_access is not None or args.share_list is not None:
    # update needed

    share_list = None
    if args.share_list is not None:
       # reset share list
       share_list = []
    if args.share_list and args.share_list != '':
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
        owner_access = int(chmod.args.owner_access) if chmod.args.owner_access else None,
        global_access = int(chmod.args.global_access) if chmod.args.global_access else None,
        share = share_list)
    obj = method(fq_name_str=chmod.args.name)
    print 'New perms %s' % print_perms(obj.get_perms2())
