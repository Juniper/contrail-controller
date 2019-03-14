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
from vnc_api.gen.vnc_api_client_gen import all_resource_types

class VncRbac(object):

    op_str = {'GET': 'R', 'POST': 'C', 'PUT': 'U', 'DELETE': 'D', 'HEAD': 'R'}
    op_str2 = {'GET': 'read', 'POST': 'create', 'PUT': 'update', 'DELETE': 'delete', 'HEAD': 'read'}

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

    def rbac_enabled(self):
        return self._server_mgr.is_rbac_enabled()

    def validate_user_visible_perm(self, id_perms, is_admin):
        return id_perms.get('user_visible', True) is not False or is_admin

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
        rule_list = []
        obj_fields = ['api_access_lists']
        try:
            (ok, result) = self._db_conn.dbe_read(obj_type, obj_uuid, obj_fields)
        except NoIdError:
            ok = False
        if not ok or 'api_access_lists' not in result:
            return []
        api_access_lists = result['api_access_lists']

        obj_fields = ['api_access_list_entries']

        for api_access_list in api_access_lists:
            (ok, result) = self._db_conn.dbe_read('api_access_list',
                                                   api_access_list['uuid'],
                                                   obj_fields)
            if not ok or 'api_access_list_entries' not in result:
                continue
            rule_list.extend(result['api_access_list_entries'].get('rbac_rule'))


        # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
        return rule_list

    def get_rbac_rules(self, request):
        rule_list = []
        env = request.headers.environ
        domain_id = env.get('HTTP_X_DOMAIN_ID')
        domain_id = str(uuid.UUID(domain_id))
        project_id = env.get('HTTP_X_PROJECT_ID')
        project_id = str(uuid.UUID(project_id))

        # get global rbac group
        config_uuid = self._db_conn.fq_name_to_uuid(
            'global_system_config', ['default-global-system-config'])
        rules = self.get_rbac_rules_object('global_system_config', config_uuid)
        rule_list.extend(rules)

        # get domain rbac group
        rules = self.get_rbac_rules_object('domain', domain_id)
        rule_list.extend(rules)

        # get project rbac group
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
            if f is None or f == '':
                f = '*'
                rule['rule_field'] = '*'
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

        return rule_dict.values()
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

    def _match_rule(self, rule, roles, api_op, err_msg):
        p = rule['rule_perms']
        role_match = [rc['role_name'] in (roles + ['*']) and
                      api_op in rc['role_crud'] for rc in p]
        match = (True in role_match)
        if match == True:
            return (True, '')
        else:
            return (False, err_msg)
    #end

    # op is one of 'CRUD'
    def validate_request(self, request):
        app = request.environ['bottle.app']
        if app.config.local_auth or self._server_mgr.is_auth_disabled():
            return (True, '')
        if not self.rbac_enabled():
            return (True, '')

        domain_id = request.headers.environ.get('HTTP_X_DOMAIN_ID')
        project_id = request.headers.environ.get('HTTP_X_PROJECT_ID')
        project_name = request.headers.environ.get('HTTP_X_PROJECT_NAME')

        user, roles = self.get_user_roles(request)

        if len(roles) == 0:
            err_msg = (401, 'roles empty!!')
            return (False, err_msg)

        is_admin = self.cloud_admin_role in roles
        # other checks redundant if admin
        if is_admin:
            return (True, '')
        if self.global_read_only_role in roles and (request.method == 'GET' or request.method == 'HEAD'):
            return (True, '')

        # rule list for project/domain of the request
        rule_list = self.get_rbac_rules(request)
        if len(rule_list) == 0:
            msg = 'rbac: rule list empty!!'
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_NOTICE)
            return (False, (403, 'Permission Denied RBAC rule list empty'))

        # object of access = 'project', 'virtual-network' ...
        obj_type = self.request_path_to_obj_type(request.path)

        # API operation create, read, update or delete
        api_op = self.op_str[request.method]

        if ((obj_type[-1:] == 's') and
            (obj_type not in (all_resource_types)) and
            (obj_type[:-1] in (all_resource_types))):
            obj_key = obj_type[:-1]
        else:
            obj_key = obj_type

        err_msg = (403, 'Permission Denied for %s as %s to %s %s in %s'
                   %(user, roles, api_op, obj_key, project_name ))
        try:
            loaded_content = request.json
        except ValueError as e:
            # Fail to decode JSON body, seems happening with certain python
            # stdlib version (Ubunutu Trusty) when the body is empty. Ignore it
            # TODO(ethuleau): don't ignore JSON decoding exceptions when we
            #                 does not support Ubuntu Trusty anymore
            # return 400, "Invalid body: %s" % str(e)
            loaded_content = None
        # Make a shallow copy of the dict body as RBAC supports only on
        # first level of fields
        if loaded_content is None:
            obj_dict = {}
        elif obj_key not in loaded_content:
            # Special API calls do not have obj_key in the POST dict
            # ie. ref-update, list-bulk-collection, set-tag...
            obj_dict = dict(loaded_content)
        elif isinstance(loaded_content[obj_key], dict):
            obj_dict = dict(loaded_content[obj_key])
        else:
            obj_dict = dict(loaded_content)

        if 'uuid' in obj_dict:
            del obj_dict['uuid']
        if 'fq_name' in obj_dict:
            del obj_dict['fq_name']

        msg = 'rbac: u=%s, r=%s, o=%s, op=%s, rules=%d, proj:%s(%s), dom:%s' \
            % (user, roles, obj_type, api_op, len(rule_list), project_id, project_name, domain_id)
        self._server_mgr.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        wildcard_rule = None
        obj_rule = None
        field_rule_list = []
        for rule in rule_list:
            if (rule['rule_object'] == '*'):
                wildcard_rule = (rule)
            elif (rule['rule_object'] == obj_key):
                if (rule['rule_field'] != '*'):
                    field_rule_list.append(rule)
                else:
                    obj_rule = (rule)
            else:
                #Not interested rule
                continue

        if field_rule_list and obj_dict:
            for rule in field_rule_list:
                f = rule['rule_field']
                if f in obj_dict:
                    match, err_msg = self._match_rule(rule, roles, api_op, err_msg)
                    if match == True:
                        del obj_dict[f]
                    else:
                        return (False, err_msg)
            if not obj_dict:
                return (True, '')
            elif (obj_rule) is not None:
                #validate against obj_rule
                return self._match_rule(obj_rule, roles, api_op, err_msg)
            elif (wildcard_rule) is not None:
                return self._match_rule(wildcard_rule, roles, api_op, err_msg)
            else:
                return (False, err_msg)
        elif (obj_rule) is not None:
            #No field rules, match obj rule permissions.
            return self._match_rule(obj_rule, roles, api_op, err_msg)
        elif (wildcard_rule) is not None:
            #No obj or field rules, match wildcard rule permissions
            return self._match_rule(wildcard_rule, roles, api_op, err_msg)
        else:
            msg = 'rbac: No interested rules!!'
            self._server_mgr.config_log(msg, level=SandeshLevel.SYS_NOTICE)
            return (False, err_msg)

    # retreive user/role from incoming request
    def get_user_roles(self, request):
        user = None
        env = request.headers.environ
        if 'HTTP_X_USER' in env:
            user = env['HTTP_X_USER']
        roles = []
        if 'HTTP_X_ROLE' in env:
            roles = env['HTTP_X_ROLE'].split(',')
        return (user, roles)
    # end get_user_roles
