#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import sys
import json
import uuid
import string
import re
from provision_defaults import *
from cfgm_common.exceptions import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class VncRbac(object):

    op_str = {'GET': 'R', 'POST': 'C', 'PUT': 'U', 'DELETE': 'D'}

    def __init__(self, rbac, server_mgr, db_conn):
        self._rbac = rbac
        self._db_conn = db_conn
        self._server_mgr = server_mgr
    # end __init__

    def set_rbac(self, value):
        self._rbac = value
    # end

    def validate_user_visible_perm(self, id_perms, is_admin):
        return id_perms.get('user_visible', True) is not False or is_admin
    # end

    def get_rbac_rules(self, request):
        rule_list = []
        env = request.headers.environ
        domain_id = env.get('HTTP_X_DOMAIN_ID', None)
        project_id = env.get('HTTP_X_PROJECT_ID', None)

        if project_id:
            project_id = str(uuid.UUID(project_id))

        if domain_id is None:
            ok = False
            try:
                (ok, result) = self._db_conn.dbe_read('project', {'uuid' : project_id},  ['fq_name'])
            except Exception as e:
                ok = False
                pass
            # if we don't know about this tenant, try default domain.
            # This can happen for service requests such as from neutron or projects not synched from keystone
            domain_name = result['fq_name'][:-1] if ok else ['default-domain']
            try:
                domain_id = self._db_conn.fq_name_to_uuid('domain', domain_name)
            except NoIdError:
                return rule_list
        else:
            x = uuid.UUID(domain_id)
            domain_id = str(x)

        # print 'project:%s, domain:%s  ' % (project_id, domain_id)

        # get domain rbac group
        obj_fields = ['api_access_lists']
        obj_ids = {'uuid' : domain_id}
        (ok, result) = self._db_conn.dbe_read('domain', obj_ids, obj_fields)
        if not ok or 'api_access_lists' not in result:
            return rule_list
        api_access_lists = result['api_access_lists']

        obj_fields = ['api_access_list_entries']
        obj_ids = {'uuid' : api_access_lists[0]['uuid']}
        (ok, result) = self._db_conn.dbe_read('api-access-list', obj_ids, obj_fields)
        if not ok or 'api_access_list_entries' not in result:
            return rule_list
        # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
        api_access_list_entries = result['api_access_list_entries']
        rule_list.extend(api_access_list_entries['rbac_rule'])

        # get project rbac group
        if project_id is None:
            return rule_list

        obj_fields = ['api_access_lists']
        obj_ids = {'uuid' : project_id}
        try:
            (ok, result) = self._db_conn.dbe_read('project', obj_ids, obj_fields)
        except Exception as e:
            ok = False
        if not ok or 'api_access_lists' not in result:
            return rule_list
        api_access_lists = result['api_access_lists']

        obj_fields = ['api_access_list_entries']
        obj_ids = {'uuid' : api_access_lists[0]['uuid']}
        (ok, result) = self._db_conn.dbe_read('api-access-list', obj_ids, obj_fields)
        if not ok or 'api_access_list_entries' not in result:
            return rule_list
        api_access_list_entries = result['api_access_list_entries']
        rule_list.extend(api_access_list_entries['rbac_rule'])

        # [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]

        return rule_list
    # end

    def request_path_to_obj_type(self, path):
        obj_type = path.split("/")[1]
        return obj_type

        """
        obj_type = request.path[1:-1]
        x = re.match("^(.*)/.*$", obj_type)
        if x:
            obj_type = x.group(1)
        """
    # end

    # op is one of 'CRUD'
    def validate_request(self, request):
        domain_id = request.headers.environ.get('HTTP_X_DOMAIN_ID', None)
        project_id = request.headers.environ.get('HTTP_X_PROJECT_ID', None)

        app = request.environ['bottle.app']
        if app.config.auth_open:
            return (True, '')
        if not self._rbac:
            return (True, '')

        err_msg = (403, 'Permission Denied')

        user, roles = self.get_user_roles(request)
        is_admin = 'admin' in [x.lower() for x in roles]

        # rule list for project/domain of the request
        rule_list = self.get_rbac_rules(request)

        # object of access = 'project', 'virtual-network' ...
        obj_type = self.request_path_to_obj_type(request.path)

        # API operation create, read, update or delete
        api_op = self.op_str[request.method]

        try:
            obj_dict = request.json[obj_type]
        except Exception:
            obj_dict = {}

        msg = 'u=%s, r=%s, o=%s, op=%s, rules=%d, proj:%s, dom:%s' \
            % (user, roles, obj_type, api_op, len(rule_list), project_id, domain_id)
        self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        # match all rules - longest prefix match wins
        result = {}
        idx = 1
        for rule in rule_list:
            o = rule['rule_object']
            f = rule['rule_field']
            p = rule['rule_perms']
            ps = ''
            for perm in p:
                ps += perm['role_name'] + ':' + perm['role_crud'] + ','
            o_f = "%s.%s" % (o,f) if f else o
            # check CRUD perms if object and field matches
            if o == '*' or \
                (o == obj_type and (f is None or f == '*' or f == '')) or \
                (o == obj_type and (f is not None and f in obj_dict)):
                if o == '*':
                    length = 0
                elif f in obj_dict:
                    length = 2
                else:
                    length = 1
                role_match = [rc['role_name'] in (roles + ['*']) and api_op in rc['role_crud'] for rc in p]
                match = True if True in role_match else False
                result[length] = (idx, match)
            msg = 'Rule %2d) %32s   %32s (%d,%s)' % (idx, o_f, ps, length, match)
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            idx += 1

        x = sorted(result.items(), reverse = True)
        ok = x[0][1][1]

        # temporarily allow all access to admin till we figure out default creation of rbac group in domain
        ok = ok or is_admin

        msg = "%s admin=%s, u=%s, r='%s'" \
            % ('+++' if ok else '\n---',
               'yes' if is_admin else 'no',
               user, string.join(roles, ',')
               )
        self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        return (True, '') if ok else (False, err_msg)
    # end validate_request

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
