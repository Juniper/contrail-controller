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
from neutron.common import constants as n_constants
from vnc_api import vnc_api

import contrail_res_handler as res_handler
import neutron_plugin_db_handler as db_handler
import router_res_handler as router_handler
import vmi_res_handler as vmi_handler


class FloatingIpMixin(object):

    def _neutron_dict_to_fip_obj(self, fip_q, is_admin=False,
                                 tenant_id=None, fip_obj=None):
        if not fip_obj:
            fip_obj = self._resource_get(id=fip_q['id'])

        vmi_get_handler = vmi_handler.VMInterfaceGetHandler(
            self._vnc_lib)
        port_id = fip_q.get('port_id')
        if port_id:
            vmi_obj = vmi_get_handler._resource_get(id=port_id)

            if not is_admin:
                vmi_tenant_id = vmi_get_handler.get_vmi_tenant_id(vmi_obj)
                if vmi_tenant_id != tenant_id:
                    self._raise_contrail_exception('PortNotFound',
                                                   resource='floatingip',
                                                   port_id=port_id)
            fip_obj.set_virtual_machine_interface(vmi_obj)
        else:
            fip_obj.set_virtual_machine_interface_list([])

        if fip_q.get('fixed_ip_address'):
            fip_obj.set_floating_ip_fixed_ip_address(fip_q['fixed_ip_address'])
        else:
            # fixed_ip_address not specified, pick from vmi_obj in create,
            # reset in case of disassociate
            vmi_refs = fip_obj.get_virtual_machine_interface_refs()
            if not vmi_refs:
                fip_obj.set_floating_ip_fixed_ip_address(None)
            else:
                vmi_obj = vmi_get_handler._resource_get(
                    back_refs=False, id=vmi_refs[0]['uuid'],
                    fields=['instance_ip_back_refs'])

                iip_refs = vmi_obj.get_instance_ip_back_refs()
                if iip_refs:
                    iip_obj = self._vnc_lib.instance_ip_read(
                        id=iip_refs[0]['uuid'])
                    fip_obj.set_floating_ip_fixed_ip_address(
                        iip_obj.get_instance_ip_address())

        return fip_obj

    def _fip_obj_to_neutron_dict(self, fip_obj):
        fip_q_dict = {}
        vmi_get_handler = vmi_handler.VMInterfaceGetHandler(
            self._vnc_lib)

        floating_net_id = self._vnc_lib.fq_name_to_id(
            'virtual-network', fip_obj.get_fq_name()[:-2])
        tenant_id = fip_obj.get_project_refs()[0]['uuid'].replace('-', '')

        port_id = None
        router_id = None
        vmi_obj = None
        vmi_refs = fip_obj.get_virtual_machine_interface_refs()
        for vmi_ref in vmi_refs or []:
            try:
                vmi_obj = vmi_get_handler._resource_get(id=vmi_ref['uuid'])
                port_id = vmi_ref['uuid']
                break
            except vnc_exc.NoIdError:
                pass

        if vmi_obj:
            router_get_handler = router_handler.LogicalRouterGetHandler(
                self._vnc_lib)
            router_id = router_get_handler.get_vmi_obj_router_id(vmi_obj)

        fip_q_dict['id'] = fip_obj.uuid
        fip_q_dict['tenant_id'] = tenant_id
        fip_q_dict['floating_ip_address'] = fip_obj.get_floating_ip_address()
        fip_q_dict['floating_network_id'] = floating_net_id
        fip_q_dict['router_id'] = router_id
        fip_q_dict['port_id'] = port_id
        fip_q_dict['fixed_ip_address'] = (
            fip_obj.get_floating_ip_fixed_ip_address())
        fip_q_dict['status'] = n_constants.PORT_STATUS_ACTIVE

        return fip_q_dict


class FloatingIpCreateHandler(res_handler.ResourceCreateHandler,
                              FloatingIpMixin):
    resource_create_method = 'floating_ip_create'

    def _create_fip_obj(self, fip_q):
        # TODO() for now create from default pool, later
        # use first available pool on net
        net_id = fip_q['floating_network_id']
        try:
            fq_name = self._vnc_lib.floating_ip_pools_list(
                parent_id=net_id)['floating-ip-pools'][0]['fq_name']
        except IndexError:
            # IndexError could happens when an attempt to
            # retrieve a floating ip pool from a private network.
            msg = "Network %s doesn't provide a floatingip pool" % net_id
            self._raise_contrail_exception('BadRequest',
                                           resource="floatingip", msg=msg)

        fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fq_name)
        fip_name = str(uuid.uuid4())
        fip_obj = vnc_api.FloatingIp(fip_name, fip_pool_obj)
        fip_obj.uuid = fip_name

        proj_id = str(uuid.UUID(fip_q['tenant_id']))
        proj_obj = self._project_read(proj_id=proj_id)
        fip_obj.set_project(proj_obj)
        return fip_obj

    def resource_create(self, context, fip_q):
        try:
            fip_obj = self._create_fip_obj(fip_q)
            fip_obj = self._neutron_dict_to_fip_obj(fip_q, context['is_admin'],
                                                    context['tenant'],
                                                    fip_obj=fip_obj)
        except Exception:
            msg = ('Internal error when trying to create floating ip. '
                   'Please be sure the network %s is an external '
                   'network.') % (fip_q['floating_network_id'])
            self._raise_contrail_exception('BadRequest',
                                           resource='floatingip', msg=msg)
        try:
            fip_uuid = self._vnc_lib.floating_ip_create(fip_obj)
        except Exception:
            self._raise_contrail_exception('IpAddressGenerationFailure',
                                           net_id=fip_q['floating_network_id'])
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_uuid)

        return self._fip_obj_to_neutron_dict(fip_obj)


class FloatingIpDeleteHandler(res_handler.ResourceDeleteHandler):
    resource_delete_method = 'floating_ip_delete'

    def resource_delete(self, fip_id):
        self._resource_delete(id=fip_id)


class FloatingIpUpdateHandler(res_handler.ResourceUpdateHandler,
                              FloatingIpMixin):
    resource_update_method = 'floating_ip_update'

    def resource_update(self, context, fip_id, fip_q):
        fip_q['id'] = fip_id
        fip_obj = self._neutron_dict_to_fip_obj(fip_q, context['is_admin'],
                                                context['tenant'])
        self._resource_update(fip_obj)
        return self._fip_obj_to_neutron_dict(fip_obj)


class FloatingIpGetHandler(res_handler.ResourceGetHandler, FloatingIpMixin):
    resource_list_method = 'floating_ips_list'
    resource_get_method = 'floating_ip_read'

    def resource_get(self, fip_uuid):
        try:
            fip_obj = self._resource_get(id=fip_uuid)
        except vnc_exc.NoIdError:
            self._raise_contrail_exception('FloatingIPNotFound',
                                           floatingip_id=fip_uuid)

        return self._fip_obj_to_neutron_dict(fip_obj)

    def resource_list(self, context, filters=None):
        # Read in floating ips with either
        # - port(s) as anchor
        # - project(s) as anchor
        # - none as anchor (floating-ip collection)
        ret_list = []

        proj_ids = None
        port_ids = None
        if filters:
            if 'tenant_id' in filters:
                proj_ids = db_handler.DBInterfaceV2._validate_project_ids(
                    context, filters['tenant_id'])
            elif 'port_id' in filters:
                port_ids = filters['port_id']
        else:  # no filters
            if not context['is_admin']:
                proj_ids = [str(uuid.UUID(context['tenant']))]

        if port_ids:
            fip_objs = self._resource_list(back_ref_id=port_ids)
        elif proj_ids:
            fip_objs = self._resource_list(back_ref_id=proj_ids)
        else:
            fip_objs = self._resource_list()

        for fip_obj in fip_objs:
            if 'floating_ip_address' in filters:
                if (fip_obj.get_floating_ip_address() not in
                        filters['floating_ip_address']):
                    continue
            ret_list.append(self._fip_obj_to_neutron_dict(fip_obj))

        return ret_list

    def resource_count(self, context, filters):
        count = self._resource_count_optimized(filters)
        if count is not None:
            return count

        floatingip_info = self.resource_list(context=context, filters=filters)
        return len(floatingip_info)


class FloatingIpHandler(FloatingIpGetHandler,
                        FloatingIpCreateHandler,
                        FloatingIpDeleteHandler,
                        FloatingIpUpdateHandler):

    pass
