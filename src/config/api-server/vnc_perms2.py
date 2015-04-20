#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import json
import string
import uuid
from provision_defaults import *
from cfgm_common.exceptions import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class color:
   PURPLE = '\033[95m'
   CYAN = '\033[96m'
   DARKCYAN = '\033[36m'
   BLUE = '\033[94m'
   GREEN = '\033[92m'
   YELLOW = '\033[93m'
   RED = '\033[91m'
   BOLD = '\033[1m'
   UNDERLINE = '\033[4m'
   END = '\033[0m'

   @staticmethod
   def cprint(msg, ok, is_admin):
        color_b = color_e = ''
        if not ok: 
            color_b = color.RED
            color_e = color.END
        elif not is_admin:
            color_b = color.BLUE
            color_e = color.END
        else:
            color_b = color.GREEN
            color_e = color.END
        print "%s%s%s" % (color_b, msg, color_e)

class VncPermissions(object):

    mode_str = {PERMS_R: 'read', PERMS_W: 'write', PERMS_X: 'link'}

    def __init__(self, server_mgr, args):
        self._server_mgr = server_mgr
    # end __init__

    @property
    def _rbac(self):
        return self._server_mgr._args.rbac
    # end

    def validate_user_visible_perm(self, id_perms2, is_admin):
        return id_perms2.get('user_visible', True) is not False or is_admin
    # end

    def validate_perms(self, request, obj_uuid, mode=PERMS_R):
        err_msg = (403, 'Permission Denied')

        user, roles = self.get_user_roles(request)
        is_admin = 'admin' in [x.lower() for x in roles]

        env = request.headers.environ
        tenant = env.get('HTTP_X_PROJECT_ID', None)
        if tenant is None:
            return (False, err_msg)
        elif '-' not in tenant:
            tenant = str(uuid.UUID(tenant))

        # retrieve object and permissions
        try:
            id_perms2 = self._server_mgr._db_conn.uuid_to_obj_perms2(obj_uuid)
        except NoIdError:
            print("Missing id_perms2 in object %s" % obj_uuid)
            return (True, '')


        owner = id_perms2['permissions']['owner']
        perms = id_perms2['permissions']['owner_access'] << 6
        if id_perms2['permissions']['globally_shared']:
            perms |= PERMS_RWX

        # build perms
        mask = 0
        if tenant == owner:
            mask |= 0700

        share = id_perms2['permissions']['share']
        tenants = [item['tenant'] for item in share]
        for item in share:
            if tenant == item['tenant']:
                perms = perms | item['tenant_access'] << 3
                mask |= 0070
                break

        if mask == 0:   # neither user nor shared
            mask = 07

        mode_mask = mode | mode << 3 | mode << 6
        ok = is_admin or (mask & perms & mode_mask)

        if ok and mode == PERMS_W:
            ok = self.validate_user_visible_perm(id_perms2, is_admin)

        msg = '%s %s %s admin=%s, mode=%03o mask=%03o perms=%03o, \
            (%s/%s/%s), user_visible=%s' \
            % ('+++' if ok else '---', self.mode_str[mode], obj_uuid,
               'yes' if is_admin else 'no', mode_mask, mask, perms,
               owner, tenant, tenants,
               id_perms2.get('user_visible', True))
        self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        color.cprint(msg, ok, is_admin)

        return (True, '') if ok else (False, err_msg)
    # end validate_perms

    # retreive user/role from incoming request
    def get_user_roles(self, request):
        user = None
        env = request.headers.environ
        if 'HTTP_X_USER' in env:
            user = env['HTTP_X_USER']
        roles = None
        if 'HTTP_X_ROLE' in env:
            roles = env['HTTP_X_ROLE'].split(',')
        return (user, roles)
    # end get_user_roles

    # set user/role in object dict from incoming request
    # called from post handler when object is created
    def set_user_role(self, request, obj_dict):
        (user, roles) = self.get_user_roles(request)
        if user:
            obj_dict['id_perms']['permissions']['owner'] = user
        if roles:
            obj_dict['id_perms']['permissions']['group'] = roles[0]
    # end set_user_role

    def check_perms_write(self, request, id):
        app = request.environ['bottle.app']
        if app.config.auth_open:
            return (True, '')

        if not self._rbac:
            return (True, '')

        return self.validate_perms(request, id, PERMS_W)
    # end check_perms_write

    def check_perms_read(self, request, id):
        app = request.environ['bottle.app']
        if app.config.auth_open:
            return (True, '')

        if not self._rbac:
            return (True, '')

        return self.validate_perms(request, id, PERMS_R)
    # end check_perms_read

    def check_perms_link(self, request, id):
        app = request.environ['bottle.app']
        if app.config.auth_open:
            return (True, '')

        if not self._rbac:
            return (True, '')

        return self.validate_perms(request, id, PERMS_X)
    # end check_perms_link
