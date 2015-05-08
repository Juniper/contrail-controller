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


class IPamMixin(object):
    def _ipam_vnc_to_neutron(self, ipam_obj):
        ipam_q_dict = self._vnc_lib.obj_to_dict(ipam_obj)

        # replace field names
        ipam_q_dict['id'] = ipam_q_dict.pop('uuid')
        ipam_q_dict['name'] = ipam_obj.name
        ipam_q_dict['tenant_id'] = ipam_obj.parent_uuid.replace('-', '')
        ipam_q_dict['mgmt'] = ipam_q_dict.pop('network_ipam_mgmt', None)
        net_back_refs = ipam_obj.get_virtual_network_back_refs()
        if net_back_refs:
            ipam_q_dict['nets_using'] = []
            for net_back_ref in net_back_refs:
                net_fq_name = net_back_ref['to']
                ipam_q_dict['nets_using'].append(net_fq_name)

        return ipam_q_dict
    # end _ipam_vnc_to_neutron

    def _ipam_neutron_to_vnc(self, ipam_q, ipam_obj):
        if 'mgmt' in ipam_q and ipam_q['mgmt']:
            ipam_obj.set_network_ipam_mgmt(
                vnc_api.IpamType.factory(**ipam_q['mgmt']))

        return ipam_obj
    # end _ipam_neutron_to_vnc


class IPamBaseGet(res_handler.ResourceGetHandler):
    resource_get_method = "network_ipam_read"


class IPamGetHandler(IPamBaseGet, IPamMixin):
    resource_list_method = "network_ipams_list"
    detail = False

    def resource_get(self, ipam_id):
        try:
            ipam_obj = self._resource_get(id=ipam_id)
        except vnc_exc.NoIdError:
            # TODO() add ipam specific exception
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'NetworkNotFound',
                net_id=ipam_id)

        return self._ipam_vnc_to_neutron(ipam_obj)

    def resource_list_by_project(self, project_id):
        project_uuid = str(uuid.UUID(project_id))

        resp_dict = self._resource_list(parent_id=project_uuid)
        return resp_dict['network-ipams']

    def resource_list(self, context=None, filters=None):
        ret_list = []

        # collect phase
        all_ipams = []  # all ipams in all projects
        if filters and 'tenant_id' in filters:
            project_ids = db_handler.DBInterfaceV2._validate_project_ids(
                context, filters['tenant_id'])
            for p_id in project_ids:
                project_ipams = self.resource_list_by_project(p_id)
                all_ipams.append(project_ipams)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_ipams = self.resource_list_by_project(proj_id)
                all_ipams.append(project_ipams)

        # prune phase
        for project_ipams in all_ipams:
            for proj_ipam in project_ipams:
                # TODO() implement same for name specified in filter
                proj_ipam_id = proj_ipam['uuid']
                if not self._filters_is_present(filters, 'id', proj_ipam_id):
                    continue
                ipam_info = self.resource_get(proj_ipam['uuid'])
                ret_list.append(ipam_info)

        return ret_list

    def resource_count(self, filters=None):
        count = self._resource_count_optimized(filters)
        if count is not None:
            return count

        ipam_info = self.resource_list(filters=filters)
        return len(ipam_info)


class IPamUpdateHandler(res_handler.ResourceUpdateHandler,
                        IPamBaseGet, IPamMixin):
    resource_update_method = "network_ipam_update"

    def resource_update(self, ipam_id, ipam_q):
        try:
            ipam_obj = self._ipam_neutron_to_vnc(
                ipam_q, self._resource_get(id=ipam_id))
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'IpamNotFound', ipam_id=ipam_id)
        self._resource_update(ipam_obj)

        return self._ipam_vnc_to_neutron(ipam_obj)


class IPamDeleteHandler(res_handler.ResourceDeleteHandler):
    resource_delete_method = "network_ipam_delete"

    def resource_delete(self, ipam_id):
        return self._resource_delete(id=ipam_id)


class IPamCreateHandler(res_handler.ResourceCreateHandler):
    resource_create_method = "network_ipam_create"

    def resource_create(self, ipam_q):
        ipam_name = ipam_q.get('name', None)
        project_id = str(uuid.UUID(ipam_q['tenant_id']))
        try:
            project_obj = self._project_read(proj_id=project_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                "ProjectNotFound", project_id=project_id)

        ipam_obj = self._ipam_neutron_to_vnc(
            ipam_q, vnc_api.NetworkIpam(ipam_name, project_obj))
        try:
            self._resource_create(ipam_obj)
        except vnc_exc.RefsExistError as e:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest',
                resource='ipam', msg=str(e))
        return self._ipam_vnc_to_neutron(ipam_obj)


class IPamHandler(IPamCreateHandler,
                  IPamUpdateHandler,
                  IPamDeleteHandler,
                  IPamGetHandler):
    pass
