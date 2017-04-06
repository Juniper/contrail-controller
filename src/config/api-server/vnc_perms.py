#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import cfgm_common
from cfgm_common import has_role
from cfgm_common import jsonutils as json
import string
import uuid
from provision_defaults import *
from cfgm_common.exceptions import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class VncPermissions(object):

    mode_str = {PERMS_R: 'R', PERMS_W: 'W', PERMS_X: 'X',
                PERMS_WX: 'WX', PERMS_RX: 'RX', PERMS_RW: 'RW', PERMS_RWX: 'RWX'}
    mode_str2 = {PERMS_R: 'read', PERMS_W: 'write', PERMS_X: 'link',
                PERMS_WX: 'write,link', PERMS_RX: 'read,link', PERMS_RW: 'read,write',
                PERMS_RWX: 'read,write,link'}

    def __init__(self, server_mgr, args):
        self._server_mgr = server_mgr
    # end __init__

    @property
    def cloud_admin_role(self):
        return self._server_mgr.cloud_admin_role

    @property
    def global_read_only_role(self):
        return self._server_mgr.global_read_only_role

    @property
    def _multi_tenancy(self):
        return self._server_mgr.is_multi_tenancy_set()
    # end

    @property
    def _rbac(self):
        return self._server_mgr.is_rbac_enabled()
    # end

    def validate_user_visible_perm(self, id_perms, is_admin):
        return id_perms.get('user_visible', True) is not False or is_admin
    # end

    def validate_perms(self, request, uuid, mode=PERMS_R, id_perms=None):
        # retrieve object and permissions
        if not id_perms:
            try:
                id_perms = self._server_mgr._db_conn.uuid_to_obj_perms(uuid)
            except NoIdError:
                return (True, 'RWX')

        err_msg = (403, 'Permission Denied')

        user, roles = self.get_user_roles(request)
        is_admin = has_role(self.cloud_admin_role, roles)
        if is_admin:
            return (True, 'RWX')
        if has_role(self.global_read_only_role, roles) and mode == PERMS_R:
            return (True, 'R')

        owner = id_perms['permissions']['owner']
        group = id_perms['permissions']['group']
        perms = id_perms['permissions']['owner_access'] << 6 | \
            id_perms['permissions']['group_access'] << 3 | \
            id_perms['permissions']['other_access']

        # check perms
        mask = 0
        if user == owner:
            mask |= 0700
        if group in roles:
            mask |= 0070
        if mask == 0:   # neither user nor group
            mask = 07

        mode_mask = mode | mode << 3 | mode << 6
        ok = (mask & perms & mode_mask)
        granted = ok & 07 | (ok >> 3) & 07 | (ok >> 6) & 07

        if ok and mode == PERMS_W:
            ok = self.validate_user_visible_perm(id_perms, is_admin)

        return (True, self.mode_str[granted]) if ok else (False, err_msg)
    # end validate_perms

    def validate_perms_rbac(self, request, obj_uuid, mode=PERMS_R, obj_owner_for_delete=None):
        err_msg = (403, 'Permission Denied')

        # retrieve object and permissions
        try:
            config = self._server_mgr._db_conn.uuid_to_obj_dict(obj_uuid)
            perms2 = config.get('prop:perms2', config.get("perms2"))
            obj_name = config.get("fq_name")
            obj_type = config.get("type")
        except NoIdError:
            return (True, '')

        user, roles = self.get_user_roles(request)
        is_admin = has_role(self.cloud_admin_role, roles)
        if is_admin:
            return (True, 'RWX')
        if has_role(self.global_read_only_role, roles) and mode == PERMS_R:
            return (True, 'R')

        env = request.headers.environ
        tenant = env.get('HTTP_X_PROJECT_ID')
        tenant_name = env.get('HTTP_X_PROJECT_NAME', '*')
        if tenant is None:
            msg = "rbac: Unable to find tenant id in headers"
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            tenant = ''
        tenant = tenant.replace('-','')

        # grant access if shared with domain
        domain = env.get('HTTP_X_DOMAIN_ID') or env.get('HTTP_X_DOMAIN_NAME') or \
            env.get('HTTP_X_USER_DOMAIN_ID') or 'default-domain'
        try:
            domain = str(uuid.UUID(domain))
        except ValueError, TypeError:
            if domain == 'default':
                domain = 'default-domain'
            domain = self._server_mgr._db_conn.fq_name_to_uuid('domain', [domain])
        if domain:
            domain = domain.replace('-','')

        owner = perms2['owner'].replace('-','')
        perms = perms2['owner_access'] << 6
        perms |= perms2['global_access']

        # build perms
        mask = 07
        if tenant == owner:
            mask |= 0700

        share = perms2['share']
        tenants = [item['tenant'] for item in share]
        for item in share:
            # item['tenant'] => [share-type, uuid]
            # allow access if domain or project from token matches configured sharing information
            (share_type, share_uuid) = cfgm_common.utils.shareinfo_from_perms2_tenant(item['tenant'])
            share_uuid = share_uuid.replace('-','')
            if ((share_type == 'tenant' and tenant == share_uuid) or (share_type == 'domain' and domain == share_uuid)):
                perms = perms | item['tenant_access'] << 3
                mask |= 0070
                break

        mode_mask = mode | mode << 3 | mode << 6
        ok = (mask & perms & mode_mask)
        if ok and obj_owner_for_delete:
            obj_owner_for_delete = obj_owner_for_delete.replace('-','')
            ok = (tenant == obj_owner_for_delete)
        granted = ok & 07 | (ok >> 3) & 07 | (ok >> 6) & 07

        msg = 'rbac: %s (%s:%s) %s %s admin=%s, mode=%03o mask=%03o perms=%03o, \
            (usr=%s(%s)/own=%s/sh=%s)' \
            % ('+++' if ok else '---', self.mode_str[mode], obj_uuid, obj_type, obj_name,
               'yes' if is_admin else 'no', mode_mask, mask, perms,
               tenant, tenant_name, owner, tenants)
        self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        if not ok:
            msg = "rbac: %s doesn't have %s permission in tenant %s" % (user, self.mode_str2[mode], owner)
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        return (True, self.mode_str[granted]) if ok else (False, err_msg)
    # end validate_perms

    # retreive user/role from incoming request
    def get_user_roles(self, request):
        user = []
        env = request.headers.environ
        if 'HTTP_X_USER' in env:
            user = env['HTTP_X_USER']
        roles = []
        if 'HTTP_X_ROLE' in env:
            roles = env['HTTP_X_ROLE'].split(',')
        return (user, roles)
    # end get_user_roles

    # set user/role in object dict from incoming request
    # called from post handler when object is created
    # TODO dsetia - no longer applicable to new RBAC permissions
    def set_user_role(self, request, obj_dict):
        (user, roles) = self.get_user_roles(request)
        if user:
            obj_dict['id_perms']['permissions']['owner'] = user
        if roles:
            obj_dict['id_perms']['permissions']['group'] = roles[0]
    # end set_user_role

    def check_perms_write(self, request, id):
        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return (True, '')

        if self._rbac:
            return self.validate_perms_rbac(request, id, PERMS_W)
        elif self._multi_tenancy:
            return self.validate_perms(request, id, PERMS_W)
        else:
            return (True, '')
    # end check_perms_write

    def check_perms_read(self, request, id, id_perms=None):
        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return (True, '')

        if self._rbac:
            return self.validate_perms_rbac(request, id, PERMS_R)
        elif self._multi_tenancy:
            return self.validate_perms(request, id, PERMS_R, id_perms)
        else:
            return (True, '')
    # end check_perms_read

    def check_perms_link(self, request, id):
        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return (True, '')

        if self._rbac:
            return self.validate_perms_rbac(request, id, PERMS_X)
        elif self._multi_tenancy:
            return self.validate_perms(request, id, PERMS_X)
        else:
            return (True, '')
    # end check_perms_link

    def check_perms_delete(self, request, obj_type, obj_uuid, parent_uuid):
        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return (True, '')

        if self._rbac:
            # delete only allowed for owner
            (ok, obj_dict) = self._server_mgr._db_conn.dbe_read(obj_type,
                             obj_uuid, obj_fields=['perms2'])
            obj_owner=obj_dict['perms2']['owner']
            return self.validate_perms_rbac(request, parent_uuid, PERMS_W, obj_owner_for_delete = obj_owner)
        elif self._multi_tenancy:
            return self.validate_perms(request, parent_uuid, PERMS_W)
        else:
            return (True, '')
    # end check_perms_write

    # This API sends perms instead of error code & message
    def obj_perms(self, request, id):
        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return 'RWX'

        if self._rbac:
            ok, perms = self.validate_perms_rbac(request, id, PERMS_RWX)
        elif self._multi_tenancy:
            ok, perms = self.validate_perms(request, id, PERMS_RWX)
        else:
            return 'RWX'

        return perms if ok else ''
    # end obj_perms
