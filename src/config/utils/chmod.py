#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import argparse
import uuid
import os

from vnc_api.vnc_api import *

class VncChmod():
    def parse_args(self):
        # Eg. python chmod.py VirtualNetwork domain:default-project:default-virtual-network

        parser = argparse.ArgumentParser(description = "Show and change object permissions and ownership")
        parser.add_argument('server', help = "API server address in the form ip:port")
        parser.add_argument('--type', help = "object type eg. virtual-network")
        parser.add_argument('--name', help = "colon seperated fully qualified name, eg "\
            "default-domain:default-project:default-virtual-network")
        parser.add_argument('--uuid',  help = "Object UUID eg f1401b7c-e387-4eec-a46c-230dfd695fae")
        parser.add_argument('--user',  help = "User Name")
        parser.add_argument('--role',  help = "Role Name")
        parser.add_argument('--owner', help = "Set owner")
        parser.add_argument('--group', help = "Set group")
        parser.add_argument('--perms', help = "Set permissions")
        parser.add_argument('--os-username',  help = "Keystone User Name", default=None)
        parser.add_argument('--os-password',  help = "Keystone User Password", default=None)
        parser.add_argument('--os-tenant-name',  help = "Keystone Tenant Name", default=None)

        self.args = parser.parse_args()
        self.opts = vars(self.args)
    #end parse_args

    def get_ks_var(self, name):
        uname = name.upper()
        cname = '-'.join(name.split('_'))
        if self.opts['os_%s' %(name)]:
            value = self.opts['os_%s' %(name)]
            return (value, '')

        rsp = ''
        try:
            value = os.environ['OS_' + uname]
            if value == '':
                value = None
        except KeyError:
            value = None

        if value is None:
            rsp = 'You must provide a %s via either --os-%s or env[OS_%s]' %(name, cname, uname)
        return (value, rsp)
    #end
#end

chmod = VncChmod()
chmod.parse_args()
conf = {}

# Validate API server information
server = chmod.args.server.split(':')
if len(server) != 2:
    print 'API server address must be of the form ip:port, for example 127.0.0.1:8082'
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
if chmod.args.group:
    print 'Group = ', chmod.args.group
if chmod.args.perms:
    print 'Perms = ', chmod.args.perms

ui = {}
if chmod.args.user:
	ui['user'] = chmod.args.user
if chmod.args.role:
	ui['role'] = chmod.args.role
if ui:
	print 'Sending user, role as %s/%s' %(chmod.args.user, chmod.args.role)

print 'Keystone credentials %s/%s/%s' %(conf['username'], conf['password'], conf['tenant_name'])
vnc = VncApi(conf['username'], conf['password'], conf['tenant_name'], server[0], server[1], user_info = ui)

if chmod.args.uuid:
    name, type = vnc.id_to_fq_name_type(chmod.args.uuid)
    chmod.args.type = type
    chmod.args.name = ":".join(name)

print 'Type = ', chmod.args.type
print 'Name = ', chmod.args.name

# read object from API server
method_name = chmod.args.type.replace('-', '_')
method = getattr(vnc, "%s_read" % (method_name))
obj = method(fq_name_str = chmod.args.name)

perms = obj.get_id_perms()
print 'Obj uuid = ', obj.uuid
print 'Obj perms = %s/%s %d%d%d' \
	%(perms.permissions.owner, perms.permissions.group, \
	  perms.permissions.owner_access, perms.permissions.group_access, perms.permissions.other_access)

write = False

# update permissions
if chmod.args.perms:
	# convert 3 digit octal permissions to owner/group/other bits
	access = list(chmod.args.perms)
	if len(access) == 4:
		access = access[1:]
	perms.permissions.owner_access = int(access[0])
	perms.permissions.group_access = int(access[1])
	perms.permissions.other_access = int(access[2])
	write = True

# update ownership
if chmod.args.owner:
	perms.permissions.owner = chmod.args.owner
	write = True

if chmod.args.group:
	perms.permissions.group = chmod.args.group
	write = True

# write to API server
if write:
	print 'New perms = %s/%s %d%d%d' \
		%(perms.permissions.owner, perms.permissions.group, \
	  	perms.permissions.owner_access, perms.permissions.group_access, perms.permissions.other_access)
	write_method = getattr(vnc, "%s_update" % (method_name))
	rv = write_method(obj)
