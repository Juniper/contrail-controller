#    Copyright
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

import uuid

from cfgm_common import exceptions as vnc_exc
from vnc_api import vnc_api
import vnc_openstack

import contrail_res_handler as res_handler
import neutron_plugin_db_handler as db_handler
import sgrule_res_handler as sgrule_handler


class SecurityGroupMixin(object):
    def _security_group_vnc_to_neutron(self, sg_obj,
                                       contrail_extensions_enabled=False):
        sg_q_dict = {}
        extra_dict = {}
        extra_dict['contrail:fq_name'] = sg_obj.get_fq_name()

        # replace field names
        sg_q_dict['id'] = sg_obj.uuid
        sg_q_dict['tenant_id'] = sg_obj.parent_uuid.replace('-', '')
        if not sg_obj.display_name:
            # for security groups created directly via vnc_api
            sg_q_dict['name'] = sg_obj.get_fq_name()[-1]
        else:
            sg_q_dict['name'] = sg_obj.display_name
        sg_q_dict['description'] = sg_obj.get_id_perms().get_description()

        # get security group rules
        sg_q_dict['security_group_rules'] = []
        rule_list = sgrule_handler.SecurityGroupRuleHandler(
            self._vnc_lib).security_group_rules_read(sg_obj)

        if rule_list:
            for rule in rule_list:
                sg_q_dict['security_group_rules'].append(rule)

        if contrail_extensions_enabled:
            sg_q_dict.update(extra_dict)
        return sg_q_dict
    # end _security_group_vnc_to_neutron

    def _security_group_neutron_to_vnc(self, sg_q, sg_vnc):
        if 'name' in sg_q and sg_q['name']:
            sg_vnc.display_name = sg_q['name']
        if 'description' in sg_q:
            id_perms = sg_vnc.get_id_perms()
            id_perms.set_description(sg_q['description'])
            sg_vnc.set_id_perms(id_perms)
        return sg_vnc
    # end _security_group_neutron_to_vnc

    def _ensure_default_security_group_exists(self, proj_id):
        proj_id = str(uuid.UUID(proj_id))
        proj_obj = self._vnc_lib.project_read(id=proj_id)
        vnc_openstack.ensure_default_security_group(self._vnc_lib, proj_obj)
    # end _ensure_default_security_group_exists


class SecurityGroupBaseGet(res_handler.ResourceGetHandler):
    resource_get_method = "security_group_read"


class SecurityGroupGetHandler(SecurityGroupBaseGet, SecurityGroupMixin):
    resource_list_method = "security_groups_list"

    def resource_get(self, sg_id, contrail_extensions_enabled=True):
        try:
            sg_obj = self._resource_get(id=sg_id)
        except vnc_exc.NoIdError:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'SecurityGroupNotFound', id=sg_id)

        return self._security_group_vnc_to_neutron(
            sg_obj, contrail_extensions_enabled)

    def resource_list_by_project(self, project_id):
        if project_id:
            try:
                project_uuid = str(uuid.UUID(project_id))
                # Trigger a project read to ensure project sync
                self._project_read(proj_id=project_uuid)
            except Exception:
                raise
        else:
            project_uuid = None

        sg_objs = self._resource_list(parent_id=project_uuid,
                                      detail=True)
        return sg_objs

    def resource_list(self, context, filters=None,
                      contrail_extensions_enabled=False):
        ret_list = []

        # collect phase
        self._ensure_default_security_group_exists(context['tenant_id'])

        all_sgs = []  # all sgs in all projects
        if context and not context['is_admin']:
            project_sgs = self.resource_list_by_project(
                str(uuid.UUID(context['tenant'])))
            all_sgs.append(project_sgs)
        else:  # admin context
            if filters and 'tenant_id' in filters:
                project_ids = db_handler.DBInterfaceV2._validate_project_ids(
                    context, filters['tenant_id'])
                for p_id in project_ids:
                    project_sgs = self.resource_list_by_project(p_id)
                    all_sgs.append(project_sgs)
            else:  # no filters
                all_sgs.append(self.resource_list_by_project(None))

        # prune phase
        for project_sgs in all_sgs:
            for sg_obj in project_sgs:
                if not db_handler.DBInterfaceV2._filters_is_present(
                        filters, 'id', sg_obj.uuid):
                    continue
                if not db_handler.DBInterfaceV2._filters_is_present(
                        filters, 'name',
                        sg_obj.get_display_name() or sg_obj.name):
                    continue
                sg_info = self._security_group_vnc_to_neutron(
                    sg_obj, contrail_extensions_enabled)
                ret_list.append(sg_info)

        return ret_list


class SecurityGroupDeleteHandler(SecurityGroupBaseGet,
                                 res_handler.ResourceDeleteHandler):
    resource_delete_method = "security_group_delete"

    def resource_delete(self, context, sg_id):
        try:
            sg_obj = self._resource_get(id=sg_id)
            if sg_obj.name == 'default' and (
               str(uuid.UUID(context['tenant_id'])) == sg_obj.parent_uuid):
                # Deny delete if the security group name is default and
                # the owner of the SG is deleting it.
                self._raise_contrail_exception(
                    'SecurityGroupCannotRemoveDefault')
        except vnc_exc.NoIdError:
            return

        try:
            self._resource_delete(sg_id)
        except vnc_exc.RefsExistError:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'SecurityGroupInUse', id=sg_id)


class SecurityGroupUpdateHandler(res_handler.ResourceUpdateHandler,
                                 SecurityGroupBaseGet,
                                 SecurityGroupMixin):
    resource_update_method = "security_group_update"

    def resource_update_obj(self, sg_obj):
        self._resource_update(sg_obj)

    def resource_update(self, sg_id, sg_q, contrail_extensions_enabled=True):
        sg_q['id'] = sg_id
        try:
            sg_obj = self._security_group_neutron_to_vnc(
                sg_q,
                self._resource_get(id=sg_id))
        except vnc_exc.NoIdError:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'SecurityGroupNotFound', id=sg_id)

        self._resource_update(sg_obj)

        ret_sg_q = self._security_group_vnc_to_neutron(
            sg_obj, contrail_extensions_enabled)

        return ret_sg_q


class SecurityGroupCreateHandler(res_handler.ResourceCreateHandler,
                                 SecurityGroupMixin):
    resource_create_method = "security_group_create"

    def _create_security_group(self, sg_q):
        project_id = str(uuid.UUID(sg_q['tenant_id']))
        try:
            project_obj = self._project_read(proj_id=project_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'ProjectNotFound', project_id=project_id)
        id_perms = vnc_api.IdPermsType(enable=True,
                                       description=sg_q.get('description'))
        sg_vnc = vnc_api.SecurityGroup(name=sg_q['name'],
                                       parent_obj=project_obj,
                                       id_perms=id_perms)
        return sg_vnc

    def resource_create(self, sg_q, contrail_extensions_enabled=False):
        sg_obj = self._security_group_neutron_to_vnc(
            sg_q,
            self._create_security_group(sg_q))

        # ensure default SG and deny create if the group name is default
        if sg_q['name'] == 'default':
            self._ensure_default_security_group_exists(sg_q['tenant_id'])
            db_handler.DBInterfaceV2._raise_contrail_exception(
                "SecurityGroupAlreadyExists")

        sg_uuid = self._resource_create(sg_obj)

        # allow all egress traffic
        def_rule = {}
        def_rule['port_range_min'] = 0
        def_rule['port_range_max'] = 65535
        def_rule['direction'] = 'egress'
        def_rule['remote_ip_prefix'] = '0.0.0.0/0'
        def_rule['remote_group_id'] = None
        def_rule['protocol'] = 'any'
        def_rule['ethertype'] = 'IPv4'
        def_rule['security_group_id'] = sg_uuid
        sgrule_handler.SecurityGroupRuleHandler(
            self._vnc_lib).resource_create(def_rule)

        ret_sg_q = self._security_group_vnc_to_neutron(
            sg_obj, contrail_extensions_enabled)
        return ret_sg_q


class SecurityGroupHandler(SecurityGroupGetHandler,
                           SecurityGroupCreateHandler,
                           SecurityGroupUpdateHandler,
                           SecurityGroupDeleteHandler):
    pass
