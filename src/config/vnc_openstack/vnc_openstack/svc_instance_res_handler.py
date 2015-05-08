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

import neutron_plugin_db_handler as db_handler
import contrail_res_handler as res_handler
import vn_res_handler as vn_handler


class SvcInstanceMixin(object):
    def _svc_instance_vnc_to_neutron(self, si_obj):
        si_q_dict = self._vnc_lib.obj_to_dict(si_obj)

        # replace field names
        si_q_dict['id'] = si_obj.uuid
        si_q_dict['tenant_id'] = si_obj.parent_uuid.replace('-', '')
        si_q_dict['name'] = si_obj.name
        si_props = si_obj.get_service_instance_properties()
        if si_props:
            vn_fq_name = si_props.get_right_virtual_network()
            vn_obj = vn_handler.VNetworkHandler(self._vnc_lib)._resource_get(
                fq_name_str=vn_fq_name)
            si_q_dict['external_net'] = str(vn_obj.uuid) + ' ' + vn_obj.name
            si_q_dict['internal_net'] = ''

        return si_q_dict
    # end _svc_instance_vnc_to_neutron


class SvcInstanceGetHandler(res_handler.ResourceGetHandler,
                            SvcInstanceMixin):
    resource_get_method = "service_instance_read"
    resource_list_method = "service_instances_list"
    detail = False

    def resource_get(self, si_id):
        try:
            si_obj = self._resource_get(id=si_id)
        except vnc_exc.NoIdError:
            # TODO add svc instance specific exception
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'NetworkNotFound', net_id=si_id)

        return self._svc_instance_vnc_to_neutron(si_obj)

    def resource_list_by_project(self, project_id):
        try:
            project_uuid = str(uuid.UUID(project_id))
        except Exception:
            print "Error in converting uuid %s" % (project_id)

        resp_dict = self._resource_list(parent_id=project_uuid)

        return resp_dict['service-instances']

    def resource_list(self, context, filters=None):
        ret_list = []

        # collect phase
        all_sis = []  # all sis in all projects
        if filters and 'tenant_id' in filters:
            project_ids = db_handler.DBInterfaceV2._validate_project_ids(
                context,
                filters['tenant_id'])
            for p_id in project_ids:
                project_sis = self.resource_list_by_project(p_id)
                all_sis.append(project_sis)
        elif filters and 'name' in filters:
            p_id = str(uuid.UUID(context['tenant']))
            project_sis = self.resource_list_by_project(p_id)
            all_sis.append(project_sis)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_sis = self.resource_list_by_project(proj_id)
                all_sis.append(project_sis)

        # prune phase
        for project_sis in all_sis:
            for proj_si in project_sis:
                # TODO implement same for name specified in filter
                proj_si_id = proj_si['uuid']
                if not self._filters_is_present(filters, 'id', proj_si_id):
                    continue
                si_info = self.resource_get(proj_si_id)
                if not self._filters_is_present(filters, 'name',
                                                si_info['name']):
                    continue
                ret_list.append(si_info)

        return ret_list


class SvcInstanceDeleteHandler(res_handler.ResourceDeleteHandler):
    resource_delete_method = "service_instance_delete"

    def resource_delete(self, si_id):
        self._resource_delete(id=si_id)


class SvcInstanceCreateHandler(res_handler.ResourceCreateHandler,
                               SvcInstanceMixin):
    resource_create_method = "service_instance_create"

    def _svc_instance_neutron_to_vnc(self, si_q):
        project_id = str(uuid.UUID(si_q['tenant_id']))
        try:
            project_obj = self._project_read(proj_id=project_id)
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'ProjectNotFound', project_id=project_id)
        net_id = si_q['external_net']
        ext_vn = vn_handler.VNetworkHandler(
            self._vnc_lib)._resource_get(id=net_id)
        scale_out = vnc_api.ServiceScaleOutType(
            max_instances=1, auto_scale=False)
        si_prop = vnc_api.ServiceInstanceType(
            auto_policy=True,
            left_virtual_network="",
            right_virtual_network=ext_vn.get_fq_name_str(),
            scale_out=scale_out)
        si_prop.set_scale_out(scale_out)
        si_vnc = vnc_api.ServiceInstance(
            name=si_q['name'],
            parent_obj=project_obj,
            service_instance_properties=si_prop)

        return si_vnc
    # end _svc_instance_neutron_to_vnc

    def resource_create(self, si_q):
        si_obj = self._svc_instance_neutron_to_vnc(si_q)
        try:
            self._resource_create(si_obj)
        except vnc_exc.RefsExistError as e:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest',
                resource='svc_instance', msg=str(e))
        st_fq_name = ['default-domain', 'nat-template']
        st_obj = self._vnc_lib.service_template_read(fq_name=st_fq_name)
        si_obj.set_service_template(st_obj)
        self._vnc_lib.service_instance_update(si_obj)

        ret_si_q = self._svc_instance_vnc_to_neutron(si_obj)
        return ret_si_q


class SvcInstanceHandler(SvcInstanceGetHandler,
                         SvcInstanceDeleteHandler,
                         SvcInstanceCreateHandler):
    pass
