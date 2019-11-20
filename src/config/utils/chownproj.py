from __future__ import print_function
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.

# Show and change all object ownership and permissions2 fields
#
from builtins import str
from builtins import object
import argparse
import uuid as __uuid
import os
import cfgm_common.exceptions

from vnc_api.vnc_api import *


class VncChmod(object):

    def parse_args(self):

        parser = argparse.ArgumentParser(
            description="Read/Change object permissions2 ownership")
        parser.add_argument(
            '--project-uuid', help="Project UUID")
        parser.add_argument(
            '--operation', help="R/U operation")
        parser.add_argument(
            '--server', help="API server address in the form ip:port",
            default = '127.0.0.1:8082')
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

ignore_list = ['floating_ips']

def print_perms(obj_perms):
    share_perms = ['%s:%d' % (x.tenant, x.tenant_access) for x in obj_perms.share]
    return '%s/%d %d %s' \
        % (obj_perms.owner, obj_perms.owner_access,
           obj_perms.global_access, share_perms)
# end print_perms


def set_perms(obj, owner=None):
    global vnc
    try:
        rv = vnc.chmod(obj.get_uuid(), owner)
        if rv is None:
            print('Error in setting perms')
    except cfgm_common.exceptions.PermissionDenied:
        print('Permission denied!')
        sys.exit(1)
# end set_perms


def read_owner_field(parent_fq_name, obj, owner):
    if (obj.children_fields is None):
        return
    else:
        for child_name in obj.children_fields:
            method = getattr(obj, "get_%s" % (child_name))
            val = method() or []
            for each in val:
                read_child = getattr(vnc, "%s_read" % (child_name[:-1]))
                try:
                    child = read_child(id=each['uuid'])
                except NoIdError:
                    print(child_name, each['uuid'], "Encountered exception")
                    continue
                read_owner_field(parent_fq_name + child.fq_name, child, owner)
                print("object_type = %s, object_name = %s, perms2.owner = %s, uuid = %s" % (child_name[:-1], child.fq_name, child.get_perms2().owner, each['uuid']))
                print()
# end


def update_owner_field(parent_fq_name, obj, owner):
    if (obj.children_fields is None):
        return
    else:
        for child_name in obj.children_fields:
            if child_name in ignore_list:
                print("Ignoring update of %s owner" %(child_name))
                continue
            method = getattr(obj, "get_%s" % (child_name))
            val = method() or []
            for each in val:
                read_child = getattr(vnc, "%s_read" % (child_name[:-1]))
                try:
                    child = read_child(id=each['uuid'])
                except Exception as e:
                    print(child_name, each['uuid'], "Encountered exception ", e)
                    continue
                update_owner_field(parent_fq_name + child.fq_name, child, owner)
                print("BEFORE UPDATE object_type = %s, object_name = %s, perms2.owner = %s, uuid = %s" % (child_name[:-1], child.fq_name, child.get_perms2().owner, each['uuid']))
                set_perms(child, owner=owner)
                child = read_child(id=each['uuid'])
                print("AFTER UPDATE object_type = %s, object_name = %s, perms2.owner = %s, uuid = %s" % (child_name[:-1], child.fq_name, child.get_perms2().owner, each['uuid']))
                print()
# end


chmod = VncChmod()
chmod.parse_args()
conf = {}
args = chmod.args

# Validate API server information
server = chmod.args.server.split(':')
if len(server) != 2:
    print('API server address must be of the form ip:port,'\
          'for example 127.0.0.1:8082')
    sys.exit(1)

# Validate keystone credentials
for name in ['username', 'password', 'tenant_name']:
    val, rsp = chmod.get_ks_var(name)
    if val is None:
        print(rsp)
        sys.exit(1)
    conf[name] = val

print('API Server = ', chmod.args.server)

vnc = VncApi(conf['username'], conf['password'], conf[
             'tenant_name'], server[0], server[1])
# end


# Update project hierarchy
def read_owner():
    project_list = vnc.projects_list()
    if chmod.args.project_uuid:
        try:
            project_uuid = str(__uuid.UUID(chmod.args.project_uuid))
            proj = vnc.project_read(id=project_uuid)
        except NoIdError:
            print('Project not present, perhaps deleted')
            sys.exit(1)
        print('PROJECT- ', proj.uuid, proj.get_perms2().owner)
        read_owner_field(proj.fq_name, proj, proj.uuid)
    else:
        for project in project_list['projects']:
            try:
                proj = vnc.project_read(id=project['uuid'])
            except NoIdError:
                print(proj.uuid, "Not present perhaps Deleted")
                continue
            print('PROJECT- ', proj.uuid, proj.get_perms2().owner)
            read_owner_field(proj.fq_name, proj, proj.uuid)
# end


def update_owner():
    project_list = vnc.projects_list()
    if chmod.args.project_uuid:
        try:
            project_uuid = str(__uuid.UUID(chmod.args.project_uuid))
            proj = vnc.project_read(id=project_uuid)
        except NoIdError:
            print('Project not present, perhaps deleted')
            sys.exit(1)
        print('PROJECT- ', proj.uuid, proj.get_perms2().owner)
        set_perms(proj, owner=proj.uuid)
        update_owner_field(proj.fq_name, proj, proj.uuid)
    else:
        for project in project_list['projects']:
            try:
                proj = vnc.project_read(id=project['uuid'])
            except NoIdError:
                print('Project not present, perhaps deleted')
                continue
            print('PROJECT- ', proj.uuid, proj.get_perms2().owner)
            set_perms(proj, owner=proj.uuid)
            update_owner_field(proj.fq_name, proj, proj.uuid)
# end


if chmod.args.operation == 'R':
    read_owner()
elif chmod.args.operation == 'U':
    update_owner()
else:
    print("--operation is needed")
    sys.exit(1)
