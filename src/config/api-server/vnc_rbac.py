#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import sys
import json
import uuid
import string
import re
import ConfigParser
from provision_defaults import *
from cfgm_common.exceptions import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class VncRbac(object):

    op_str = {'GET': 'R', 'POST': 'C', 'PUT': 'U', 'DELETE': 'D'}
    op_str2 = {'GET': 'read', 'POST': 'create', 'PUT': 'update', 'DELETE': 'delete'}

    def __init__(self, server_mgr, db_conn):
        self._db_conn = db_conn
        self._server_mgr = server_mgr
    # end __init__

    @property
    def cloud_admin_role(self):
        return self._server_mgr.cloud_admin_role

    @property
    def global_read_only_role(self):
        return self._server_mgr.global_read_only_role

    def multi_tenancy_with_rbac(self):
        return self._server_mgr.is_rbac_enabled()
    # end

    def validate_user_visible_perm(self, id_perms, is_admin):
        return id_perms.get('user_visible', True) is not False or is_admin
    # end

    def read_default_rbac_rules(self, conf_file):
        config = ConfigParser.SafeConfigParser()
        config.read(conf_file)
        raw_rules = {}
        if 'default-domain' in config.sections():
            raw_rules = dict(config.items('default-domain'))

        rbac_rules = []
        for lhs, rhs in raw_rules.items():
            # lhs is object.field, rhs is list of perms
            obj_field = lhs.split(".")
            perms = rhs.split(",")

            # perms ['foo:CRU', 'bar:CR']
            role_to_crud_dict = {}
            for perm in perms:
                p = perm.split(":")
                # both role and crud must be specified
                if len(p) < 2:
                    continue
                # <crud> must be [CRUD]
                rn = p[0].strip()
                rc = p[1].strip()
                if not set(rc).issubset(set('CRUD')):
                    continue
                role_to_crud_dict[rn] = rc
            if len(role_to_crud_dict) == 0:
                continue

            rule = {
                'rule_object': obj_field[0],
                'rule_field' : obj_field[1] if len(obj_field) > 1 else None,
                'rule_perms' : [{'role_name':rn, 'role_crud':rc} for rn,rc in role_to_crud_dict.items()],
            }
            rbac_rules.append(rule)
        return rbac_rules

    def get_rbac_rules_object(self, obj_type, obj_uuid):
        obj_fields = ['api_access_lists']
        try:
            (ok, result) = self._db_conn.dbe_read(obj_type, obj_uuid, obj_fields)
        except NoIdError:
            ok = False
        if not ok or 'api_access_lists' not in result:
            return []
        api_access_lists = result['api_access_lists']

        obj_fields = ['api_access_list_entries']
        (ok, result) = self._db_conn.dbe_read(
            'api_access_list', api_access_lists[0]['uuid'], obj_fields)
        if not ok or 'api_access_list_entries' not in result:
            return []
        # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
        api_access_list_entries = result['api_access_list_entries']
        return api_access_list_entries['rbac_rule']

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
                (ok, result) = self._db_conn.dbe_read('project', project_id,  ['fq_name'])
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

        # get global rbac group
        config_uuid = self._db_conn.fq_name_to_uuid('global_system_config', ['default-global-system-config'])
        rules = self.get_rbac_rules_object('global_system_config', config_uuid)
        rule_list.extend(rules)

        # get domain rbac group
        rules = self.get_rbac_rules_object('domain', domain_id)
        rule_list.extend(rules)

        # get project rbac group
        if project_id is None:
            return rule_list

        rules = self.get_rbac_rules_object('project', project_id)
        rule_list.extend(rules)

        # [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]

        # collapse of rules might be needed as same object/field might be present in
        # domain as well project rules
        rule_dict = {}
        for rule in rule_list:
            o = rule['rule_object']
            f = rule['rule_field']
            p = rule['rule_perms']
            o_f = "%s.%s" % (o,f) if f else o
            if o_f not in rule_dict:
                rule_dict[o_f] = rule
            else:
                role_to_crud_dict = {rp['role_name']:rp['role_crud'] for rp in rule_dict[o_f]['rule_perms']}
                for incoming in rule['rule_perms']:
                    role_name = incoming['role_name']
                    role_crud = incoming['role_crud']
                    if role_name in role_to_crud_dict:
                        x = set(list(role_to_crud_dict[role_name])) | set(list(role_crud))
                        role_to_crud_dict[role_name] = ''.join(x)
                    else:
                        role_to_crud_dict[role_name] = role_crud
                # update perms in existing rule
                rule_dict[o_f]['rule_perms'] = [{'role_crud': rc, 'role_name':rn} for rn,rc in role_to_crud_dict.items()]
                # remove duplicate rule from list
                rule_list.remove(rule)

        return rule_list
    # end

    def request_path_to_obj_type(self, path):
        if path == "/":
            return path
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
        project_name = request.headers.environ.get('HTTP_X_PROJECT_NAME', '*')

        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return (True, '')
        if not self.multi_tenancy_with_rbac():
            return (True, '')

        err_msg = (403, 'Permission Denied')

        user, roles = self.get_user_roles(request)
        is_admin = self.cloud_admin_role in roles
        # other checks redundant if admin
        if is_admin:
            return (True, '')
        if self.global_read_only_role in roles and request.method == 'GET':
            return (True, '')

        # rule list for project/domain of the request
        rule_list = self.get_rbac_rules(request)
        if len(rule_list) == 0:
            msg = 'rbac: rule list empty!!'
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_NOTICE)
            return (False, err_msg)

        # object of access = 'project', 'virtual-network' ...
        obj_type = self.request_path_to_obj_type(request.path)

        # API operation create, read, update or delete
        api_op = self.op_str[request.method]

        try:
            obj_dict = request.json[obj_type]
        except Exception:
            obj_dict = {}

        msg = 'rbac: u=%s, r=%s, o=%s, op=%s, rules=%d, proj:%s(%s), dom:%s' \
            % (user, roles, obj_type, api_op, len(rule_list), project_id, project_name, domain_id)
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
            length = -1; match = False
            if o == '*' or \
                (o == obj_type and (f is None or f == '*' or f == '')) or \
                (o == obj_type and (f is not None and f in obj_dict)):
                if o == '*':
                    length = 0
                elif f in obj_dict:
                    length = 2
                else:
                    length = 1
                # skip rule with no matching op
                if True not in [api_op in rc['role_crud'] for rc in p]:
                    continue
                role_match = [rc['role_name'] in (roles + ['*']) and api_op in rc['role_crud'] for rc in p]
                match = True if True in role_match else False
                result[length] = (idx, match)
            msg = 'Rule %2d) %32s   %32s (%d,%s)' % (idx, o_f, ps, length, match)
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            idx += 1

        ok = False
        if len(result) > 0:
            x = sorted(result.items(), reverse = True)
            ok = x[0][1][1]

        msg = "rbac: %s admin=%s, u=%s, r='%s'" \
            % ('+++' if ok else '\n---',
               'yes' if is_admin else 'no',
               user, string.join(roles, ',')
               )
        self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        if not ok:
            msg = "rbac: %s doesn't have %s permission for %s" % (user, self.op_str2[request.method], obj_type)
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_NOTICE)

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
