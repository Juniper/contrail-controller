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

import contrail_res_handler as res_handler
import neutron_plugin_db_handler as db_handler


class PolicyMixin(object):
    def _policy_vnc_to_neutron(self, policy_obj):
        policy_q_dict = self._vnc_lib.obj_to_dict(policy_obj)

        # replace field names
        policy_q_dict['id'] = policy_q_dict.pop('uuid')
        policy_q_dict['name'] = policy_obj.name
        policy_q_dict['tenant_id'] = policy_obj.parent_uuid.replace('-', '')
        policy_q_dict['entries'] = policy_q_dict.pop('network_policy_entries',
                                                     None)
        net_back_refs = policy_obj.get_virtual_network_back_refs()
        if net_back_refs:
            policy_q_dict['nets_using'] = []
            for net_back_ref in net_back_refs:
                net_fq_name = net_back_ref['to']
                policy_q_dict['nets_using'].append(net_fq_name)

        return policy_q_dict
    # end _policy_vnc_to_neutron

    def _policy_neutron_to_vnc(self, policy_q, policy_obj):
        if 'entries' in policy_q:
            policy_obj.set_network_policy_entries(
                vnc_api.PolicyEntriesType.factory(**policy_q['entries']))

        return policy_obj
    # end _policy_neutron_to_vnc


class PolicyBaseGet(res_handler.ResourceGetHandler):
    resource_get_method = "network_policy_read"


class PolicyGetHandler(PolicyBaseGet, PolicyMixin):
    resource_list_method = "network_policys_list"
    detail = False

    def resource_get(self, policy_id):
        try:
            policy_obj = self._resource_get(id=policy_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                "PolicyNotFound", id=policy_id)

        return self._policy_vnc_to_neutron(policy_obj)

    def resource_list_by_project(self, project_id):
        project_uuid = str(uuid.UUID(project_id))
        resp_dict = self._resource_list(parent_id=project_uuid)

        return resp_dict['network-policys']

    def resource_list(self, context=None, filters=None):
        ret_list = []

        # collect phase
        all_policys = []  # all policys in all projects
        if filters and 'tenant_id' in filters:
            project_ids = db_handler.DBInterfaceV2._validate_project_ids(
                context,
                filters['tenant_id'])
            for p_id in project_ids:
                project_policys = self.resource_list_by_project(p_id)
                all_policys.append(project_policys)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_policys = self.resource_list_by_project(proj_id)
                all_policys.append(project_policys)

        # prune phase
        for project_policys in all_policys:
            for proj_policy in project_policys:
                # TODO() implement same for name specified in filter
                proj_policy_id = proj_policy['uuid']
                if not self._filters_is_present(filters, 'id', proj_policy_id):
                    continue
                policy_info = self.resource_get(proj_policy['uuid'])
                ret_list.append(policy_info)

        return ret_list

    def resource_count(self, context, filters=None):
        count = self._resource_count_optimized(filters)
        if count is not None:
            return count

        policy_info = self.resource_list(filters=filters)
        return len(policy_info)


class PolicyCreateHandler(res_handler.ResourceCreateHandler, PolicyMixin):
    resource_create_method = "network_policy_create"

    def resource_create(self, policy_q):
        if 'tenant_id' not in policy_q:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest', resource='policy',
                msg="'tenant_id' is mandatory")
        project_id = str(uuid.UUID(policy_q['tenant_id']))
        policy_name = policy_q.get('name', None)
        try:
            project_obj = self._project_read(proj_id=project_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                "ProjectNotFound", id=project_id)

        policy_obj = vnc_api.NetworkPolicy(policy_name, project_obj)
        policy_obj = self._policy_neutron_to_vnc(policy_q, policy_obj)
        try:
            self._resource_create(policy_obj)
        except vnc_exc.RefsExistError as e:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest',
                resource='policy', msg=str(e))
        return self._policy_vnc_to_neutron(policy_obj)


class PolicyUpdateHandler(res_handler.ResourceUpdateHandler,
                          PolicyBaseGet, PolicyMixin):
    resource_update_method = "network_policy_update"

    def resource_update(self, policy_id, policy_q):
        policy_q['id'] = policy_id
        try:
            policy_obj = self._policy_neutron_to_vnc(
                policy_q, self._resource_get(id=policy_q['id']))
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                "PolicyNotFound", id=policy_id)
        self._resource_update(policy_obj)

        return self._policy_vnc_to_neutron(policy_obj)


class PolicyDeleteHandler(res_handler.ResourceDeleteHandler):
    resource_delete_method = "network_policy_delete"

    def resource_delete(self, policy_id):
        try:
            self._resource_delete(id=policy_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                "PolicyNotFound", id=policy_id)


class PolicyHandler(PolicyGetHandler,
                    PolicyCreateHandler,
                    PolicyUpdateHandler,
                    PolicyDeleteHandler):
    pass
