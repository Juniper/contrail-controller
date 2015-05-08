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


class RouteTableMixin(object):
    def _route_table_vnc_to_neutron(self, rt_obj):
        rt_q_dict = self._vnc_lib.obj_to_dict(rt_obj)

        # replace field names
        rt_q_dict['id'] = rt_obj.uuid
        rt_q_dict['tenant_id'] = rt_obj.parent_uuid.replace('-', '')
        rt_q_dict['name'] = rt_obj.name
        rt_q_dict['fq_name'] = rt_obj.fq_name

        # get route table routes
        rt_q_dict['routes'] = rt_q_dict.pop('routes', None)
        if rt_q_dict['routes']:
            for route in rt_q_dict['routes']['route']:
                if route['next_hop_type']:
                    route['next_hop'] = route['next_hop_type']

        return rt_q_dict
    # end _route_table_vnc_to_neutron


class RouteTableBaseGet(res_handler.ResourceGetHandler):
    resource_get_method = "route_table_read"


class RouteTableGetHandler(RouteTableBaseGet,
                           RouteTableMixin):
    resource_list_method = "route_tables_list"
    detail = False

    def resource_get(self, rt_id):
        try:
            rt_obj = self._resource_get(id=rt_id)
        except vnc_exc.NoIdError:
            # TODO() add route table specific exception
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'NetworkNotFound', net_id=rt_id)

        return self._route_table_vnc_to_neutron(rt_obj)

    def resource_list_by_project(self, project_id):
        try:
            project_uuid = str(uuid.UUID(project_id))
        except Exception:
            print("Error in converting uuid %s" % (project_id))

        resp_dict = self._resource_list(parent_id=project_uuid)

        return resp_dict['route-tables']

    def resource_list(self, context, filters=None):
        ret_list = []

        # collect phase
        all_rts = []  # all rts in all projects
        if filters and 'tenant_id' in filters:
            project_ids = db_handler.DBInterfaceV2._validate_project_ids(
                context,
                filters['tenant_id'])
            for p_id in project_ids:
                project_rts = self.resource_list_by_project(p_id)
                all_rts.append(project_rts)
        elif filters and 'name' in filters:
            p_id = str(uuid.UUID(context['tenant']))
            project_rts = self.resource_list_by_project(p_id)
            all_rts.append(project_rts)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_rts = self.resource_list_by_project(proj_id)
                all_rts.append(project_rts)

        # prune phase
        for project_rts in all_rts:
            for proj_rt in project_rts:
                # TODO() implement same for name specified in filter
                proj_rt_id = proj_rt['uuid']
                if not self._filters_is_present(filters, 'id', proj_rt_id):
                    continue
                rt_info = self.resource_get(proj_rt_id)
                if not self._filters_is_present(filters, 'name',
                                                rt_info['name']):
                    continue
                ret_list.append(rt_info)

        return ret_list


class RouteTableCreateHandler(res_handler.ResourceCreateHandler):
    resource_create_method = "route_table_create"

    def resource_create(self, rt_q):
        project_id = str(uuid.UUID(rt_q['tenant_id']))
        project_obj = self._project_read(proj_id=project_id)
        rt_obj = vnc_api.RouteTable(name=rt_q['name'],
                                    parent_obj=project_obj)

        if rt_q['routes']:
            for route in rt_q['routes']['route']:
                try:
                    vm_obj = self._vnc_lib.virtual_machine_read(
                        id=route['next_hop'])
                    si_list = vm_obj.get_service_instance_refs()
                    if si_list:
                        fq_name = si_list[0]['to']
                        si_obj = self._vnc_lib.service_instance_read(
                            fq_name=fq_name)
                        route['next_hop'] = si_obj.get_fq_name_str()
                    rt_obj.set_routes(
                        vnc_api.RouteTableType.factory(**rt_q['routes']))
                except Exception as e:
                    pass
        try:
            self._resource_create(rt_obj)
        except vnc_exc.RefsExistError as e:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest',
                resource='route_table', msg=str(e))
        ret_rt_q = self._route_table_vnc_to_neutron(rt_obj)
        return ret_rt_q


class RouteTableUpdateHandler(res_handler.ResourceUpdateHandler,
                              RouteTableBaseGet,
                              RouteTableMixin):
    resource_update_method = "route_table_update"

    def resource_update(self, rt_id, rt_q):
        rt_q['id'] = rt_id
        try:
            rt_obj = self._resource_get(id=rt_q['id'])
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'ResourceNotFound', id=rt_id)

        if rt_q['routes']:
            for route in rt_q['routes']['route']:
                try:
                    vm_obj = self._vnc_lib.virtual_machine_read(
                        id=route['next_hop'])
                    si_list = vm_obj.get_service_instance_refs()
                    if si_list:
                        fq_name = si_list[0]['to']
                        si_obj = self._vnc_lib.service_instance_read(
                            fq_name=fq_name)
                        route['next_hop'] = si_obj.get_fq_name_str()
                    rt_obj.set_routes(
                        vnc_api.RouteTableType.factory(**rt_q['routes']))
                except Exception:
                    pass
        self._resource_update(rt_obj)
        return self._route_table_vnc_to_neutron(rt_obj)


class RouteTableDeleteHandler(res_handler.ResourceDeleteHandler):
    resource_delete_method = "route_table_delete"

    def resource_delete(self, rt_id):
        try:
            self._resource_delete(rt_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                "ResourceNotFound", id=rt_id)


class RouteTableHandler(RouteTableGetHandler,
                        RouteTableCreateHandler,
                        RouteTableUpdateHandler,
                        RouteTableDeleteHandler):
    pass
