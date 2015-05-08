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
import netaddr
from neutron.common import constants as n_constants
from neutron.common import exceptions as n_exceptions
from vnc_api import vnc_api


import contrail_res_handler as res_handler
import neutron_plugin_db_handler as db_handler
import subnet_res_handler as subnet_handler
import vmi_res_handler as vmi_handler

SNAT_SERVICE_TEMPLATE_FQ_NAME = ['default-domain', 'netns-snat-template']


class LogicalRouterMixin(object):

    @staticmethod
    def _get_external_gateway_info(rtr_obj):
        vn_refs = rtr_obj.get_virtual_network_refs()
        if vn_refs:
            return vn_refs[0]['uuid']

    def _neutron_dict_to_rtr_obj(self, router_q, rtr_obj):
        rtr_name = router_q.get('name')
        id_perms = rtr_obj.get_id_perms()
        if 'admin_state_up' in router_q:
            id_perms.enable = router_q['admin_state_up']
            rtr_obj.set_id_perms(id_perms)

        if rtr_name:
            rtr_obj.display_name = rtr_name

        return rtr_obj

    def _rtr_obj_to_neutron_dict(self, rtr_obj,
                                 contrail_extensions_enabled=True):
        rtr_q_dict = {}

        rtr_q_dict['id'] = rtr_obj.uuid
        if not rtr_obj.display_name:
            rtr_q_dict['name'] = rtr_obj.get_fq_name()[-1]
        else:
            rtr_q_dict['name'] = rtr_obj.display_name
        rtr_q_dict['tenant_id'] = rtr_obj.parent_uuid.replace('-', '')
        rtr_q_dict['admin_state_up'] = rtr_obj.get_id_perms().enable
        rtr_q_dict['shared'] = False
        rtr_q_dict['status'] = n_constants.NET_STATUS_ACTIVE
        rtr_q_dict['gw_port_id'] = None

        ext_net_uuid = self._get_external_gateway_info(rtr_obj)
        if not ext_net_uuid:
            rtr_q_dict['external_gateway_info'] = None
        else:
            rtr_q_dict['external_gateway_info'] = {'network_id': ext_net_uuid,
                                                   'enable_snat': True}

        if contrail_extensions_enabled:
            rtr_q_dict.update({'contrail:fq_name': rtr_obj.get_fq_name()})
        return rtr_q_dict

    def _router_add_gateway(self, router_q, rtr_obj):
        ext_gateway = router_q.get('external_gateway_info')
        old_ext_gateway = self._get_external_gateway_info(rtr_obj)
        if ext_gateway or old_ext_gateway:
            network_id = None
            if ext_gateway:
                network_id = ext_gateway.get('network_id')
            if network_id:
                if old_ext_gateway and network_id == old_ext_gateway:
                    return
                try:
                    vn_obj = self._vnc_lib.virtual_network_read(id=network_id)
                    if not vn_obj.get_router_external():
                        self._raise_contrail_exception(
                            'BadRequest', resource='router',
                            msg="Network %s is not a valid "
                                "external network" % network_id)
                except vnc_exc.NoIdError:
                    self._raise_contrail_exception('NetworkNotFound',
                                                   net_id=network_id)

                self._router_set_external_gateway(rtr_obj, vn_obj)
            else:
                self._router_clear_external_gateway(rtr_obj)

    def _router_set_external_gateway(self, router_obj, ext_net_obj):
        project_obj = self._project_read(proj_id=router_obj.parent_uuid)

        # Get netns SNAT service template
        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=SNAT_SERVICE_TEMPLATE_FQ_NAME)
        except vnc_exc.NoIdError:
            self._raise_contrail_exception(
                'BadRequest', resouce='router',
                msg="Unable to set or clear the default gateway")

        # Get the service instance if it exists
        si_name = 'si_' + router_obj.uuid
        si_fq_name = project_obj.get_fq_name() + [si_name]
        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
        except vnc_exc.NoIdError:
            si_obj = None

        # Get route table for default route it it exists
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.get_fq_name() + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            rt_obj = None

        # Set the service instance
        si_created = False
        if not si_obj:
            si_obj = vnc_api.ServiceInstance(si_name, parent_obj=project_obj)
            si_created = True

        si_prop_obj = vnc_api.ServiceInstanceType(
            scale_out=vnc_api.ServiceScaleOutType(max_instances=2,
                                                  auto_scale=True),
            auto_policy=True)

        # set right interface in order of [right, left] to match template
        left_if = vnc_api.ServiceInstanceInterfaceType()
        right_if = vnc_api.ServiceInstanceInterfaceType(
            virtual_network=ext_net_obj.get_fq_name_str())
        si_prop_obj.set_interface_list([right_if, left_if])
        si_prop_obj.set_ha_mode('active-standby')

        si_obj.set_service_instance_properties(si_prop_obj)
        si_obj.set_service_template(st_obj)
        if si_created:
            self._vnc_lib.service_instance_create(si_obj)
        else:
            self._vnc_lib.service_instance_update(si_obj)

        # Set the route table
        route_obj = vnc_api.RouteType(prefix="0.0.0.0/0",
                                      next_hop=si_obj.get_fq_name_str())
        rt_created = False
        if not rt_obj:
            rt_obj = vnc_api.RouteTable(name=rt_name, parent_obj=project_obj)
            rt_created = True

        rt_obj.set_routes(vnc_api.RouteTableType.factory([route_obj]))

        if rt_created:
            self._vnc_lib.route_table_create(rt_obj)
        else:
            self._vnc_lib.route_table_update(rt_obj)

        # Associate route table to all private networks connected onto
        # that router
        vmi_get_handler = vmi_handler.VMInterfaceGetHandler(self._vnc_lib)
        for intf in router_obj.get_virtual_machine_interface_refs() or []:
            port_id = intf['uuid']
            net_id = vmi_get_handler.resource_get(
                port_id=port_id)['network_id']
            try:
                vn_obj = self._vnc_lib.virtual_network_read(id=net_id)
            except vnc_exc.NoIdError:
                self._raise_contrail_exception(
                    'NetworkNotFound', net_id=net_id)
            vn_obj.set_route_table(rt_obj)
            self._vnc_lib.virtual_network_update(vn_obj)

        # Add logical gateway virtual network
        router_obj.set_service_instance(si_obj)
        router_obj.set_virtual_network(ext_net_obj)
        self._vnc_lib.logical_router_update(router_obj)

    def _router_clear_external_gateway(self, router_obj):
        project_obj = self._project_read(proj_id=router_obj.parent_uuid)

        # Get the service instance if it exists
        si_name = 'si_' + router_obj.uuid
        si_fq_name = project_obj.get_fq_name() + [si_name]
        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
            si_uuid = si_obj.uuid
        except vnc_exc.NoIdError:
            si_obj = None

        # Get route table for default route it it exists
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.get_fq_name() + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            rt_obj = None

        # Delete route table
        if rt_obj:
            # Disassociate route table to all private networks connected
            # onto that router
            for net_ref in rt_obj.get_virtual_network_back_refs() or []:
                try:
                    vn_obj = self._vnc_lib.virtual_network_read(
                        id=net_ref['uuid'])
                except vnc_exc.NoIdError:
                    continue
                vn_obj.del_route_table(rt_obj)
                self._vnc_lib.virtual_network_update(vn_obj)
            self._vnc_lib.route_table_delete(id=rt_obj.uuid)

        # Clear logical gateway virtual network
        router_obj.set_virtual_network_list([])
        router_obj.set_service_instance_list([])
        self._vnc_lib.logical_router_update(router_obj)

        # Delete service instance
        if si_obj:
            self._vnc_lib.service_instance_delete(id=si_uuid)

    def _set_snat_routing_table(self, router_obj, network_id):
        project_obj = self._project_read(proj_id=router_obj.parent_uuid)
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.get_fq_name() + [rt_name]

        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            # No route table set with that router ID, the gateway is not set
            return

        try:
            vn_obj = self._vnc_lib.virtual_network_read(id=network_id)
        except vnc_exc.NoIdError:
            raise n_exceptions.NetworkNotFound(net_id=network_id)

        vn_obj.set_route_table(rt_obj)
        self._vnc_lib.virtual_network_update(vn_obj)

    def _clear_snat_routing_table(self, router_obj, network_id):
        project_obj = self._project_read(proj_id=router_obj.parent_uuid)
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.get_fq_name() + [rt_name]

        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            # No route table set with that router ID, the gateway is not set
            return

        try:
            vn_obj = self._vnc_lib.virtual_network_read(id=network_id)
        except vnc_exc.NoIdError:
            raise n_exceptions.NetworkNotFound(net_id=network_id)
        vn_obj.del_route_table(rt_obj)
        self._vnc_lib.virtual_network_update(vn_obj)


class LogicalRouterCreateHandler(res_handler.ResourceCreateHandler,
                                 LogicalRouterMixin):
    resource_create_method = 'logical_router_create'

    def _create_router(self, router_q):
        project_id = str(uuid.UUID(router_q['tenant_id']))
        project_obj = self._project_read(proj_id=project_id)
        id_perms = vnc_api.IdPermsType(enable=True)
        return vnc_api.LogicalRouter(router_q.get('name'), project_obj,
                                     id_perms=id_perms)

    def resource_create(self, router_q, contrail_extensions_enabled=True):
        rtr_obj = self._neutron_dict_to_rtr_obj(
            router_q, self._create_router(router_q))
        rtr_uuid = self._resource_create(rtr_obj)

        # read it back to update id perms
        rtr_obj = self._resource_get(id=rtr_uuid)
        self._router_add_gateway(router_q, rtr_obj)
        return self._rtr_obj_to_neutron_dict(
            rtr_obj, contrail_extensions_enabled=contrail_extensions_enabled)


class LogicalRouterDeleteHandler(res_handler.ResourceDeleteHandler,
                                 LogicalRouterMixin):
    resource_delete_method = 'logical_router_delete'

    def resource_delete(self, rtr_id):
        try:
            rtr_obj = self._resource_get(id=rtr_id)
            if rtr_obj.get_virtual_machine_interface_refs():
                self._raise_contrail_exception('RouterInUse',
                                               router_id=rtr_id)
        except vnc_exc.NoIdError:
            self._raise_contrail_exception('RouterNotFound',
                                           router_id=rtr_id)

        self._router_clear_external_gateway(rtr_obj)
        try:
            self._resource_delete(id=rtr_id)
        except vnc_exc.RefsExistError:
            self._raise_contrail_exception('RouterInUse', router_id=rtr_id)


class LogicalRouterUpdateHandler(res_handler.ResourceUpdateHandler,
                                 LogicalRouterMixin):
    resource_update_method = 'logical_router_update'

    def _get_rtr_obj(self, router_q):
        return self._resource_get(id=router_q.get('id'))

    def resource_update(self, router_q, rtr_id):
        router_q['id'] = rtr_id
        rtr_obj = self._neutron_dict_to_rtr_obj(
            router_q, self._get_rtr_obj(router_q))
        self._resource_update(rtr_obj)
        self._router_add_gateway(router_q, rtr_obj)
        return self._rtr_obj_to_neutron_dict(rtr_obj)


class LogicalRouterGetHandler(res_handler.ResourceGetHandler,
                              LogicalRouterMixin):
    resource_get_method = 'logical_router_read'
    resource_list_method = 'logical_routers_list'

    def _router_list_project(self, project_id=None, detail=False):
        if project_id:
            try:
                project_uuid = str(uuid.UUID(project_id))
            except Exception:
                return []
        else:
            project_uuid = None

        resp = self._resource_list(parent_id=project_uuid,
                                   detail=detail)
        if detail:
            return resp

        return resp['logical-routers']

    def _get_router_list_for_ids(self, rtr_ids, extensions_enabled=True):
        ret_list = []
        for rtr_id in rtr_ids or []:
            try:
                rtr_obj = self._resource_get(id=rtr_id)
                rtr_info = self._rtr_obj_to_neutron_dict(
                    rtr_obj,
                    contrail_extensions_enabled=extensions_enabled)
                ret_list.append(rtr_info)
            except vnc_exc.NoIdError:
                pass
        return ret_list

    def _get_router_list_for_project(self, project_id=None):
        project_rtrs = self._router_list_project(project_id=project_id)
        rtr_uuids = [rtr['uuid'] for rtr in project_rtrs]
        return self._get_router_list_for_ids(rtr_uuids)

    def _fip_pool_ref_routers(self, project_id):
        """TODO."""
        return []

    def get_vmi_obj_router_id(self, vmi_obj, project_id=None):
        vmi_get_handler = vmi_handler.VMInterfaceGetHandler(
            self._vnc_lib)

        port_net_id = vmi_obj.get_virtual_network_refs()[0]['uuid']
        # find router_id from port
        router_list = self._router_list_project(project_id=project_id,
                                                detail=True)
        for router_obj in router_list or []:
            for vmi in (router_obj.get_virtual_machine_interface_refs()
                        or []):
                vmi_obj = vmi_get_handler._resource_get(id=vmi['uuid'])
                if (vmi_obj.get_virtual_network_refs()[0]['uuid'] ==
                        port_net_id):
                    return router_obj.uuid

    def resource_get(self, rtr_uuid, fields=None):
        try:
            rtr_obj = self._resource_get(id=rtr_uuid)
        except vnc_exc.NoIdError:
            self._raise_contrail_exception('RouterNotFound',
                                           router_id=rtr_uuid)

        return self._rtr_obj_to_neutron_dict(rtr_obj)

    def resource_list(self, **kwargs):
        context = kwargs.get('context')
        filters = kwargs.get('filters')
        extensions_enabled = kwargs.get('contrail_extensions_enabled', True)
        ret_list = []

        if filters and 'shared' in filters:
            if filters['shared'][0]:
                # no support for shared routers
                return ret_list

        if not filters:
            return self._get_router_list_for_project()

        all_rtrs = []  # all n/ws in all projects
        if 'id' in filters:
            return self._get_router_list_for_ids(filters['id'],
                                                 extensions_enabled)

        if 'tenant_id' in filters:
            # read all routers in project, and prune below
            project_ids = db_handler.DBInterfaceV2._validate_project_ids(
                context, project_ids=filters['tenant_id'])
            for p_id in project_ids:
                if 'router:external' in filters:
                    all_rtrs.append(self._fip_pool_ref_routers(p_id))
                else:
                    project_rtrs = self._router_list_project(p_id)
                    all_rtrs.append(project_rtrs)

        else:
            # read all routers in all projects
            project_rtrs = self._router_list_project()
            all_rtrs.append(project_rtrs)

        # prune phase
        for project_rtrs in all_rtrs:
            for proj_rtr in project_rtrs:
                proj_rtr_id = proj_rtr['uuid']
                if not self._filters_is_present(filters, 'id', proj_rtr_id):
                    continue

                proj_rtr_fq_name = unicode(proj_rtr['fq_name'])
                if not self._filters_is_present(filters, 'contrail:fq_name',
                                                proj_rtr_fq_name):
                    continue
                try:
                    rtr_obj = self._resource_get(id=proj_rtr['uuid'])
                    if not self._filters_is_present(
                            filters, 'name',
                            rtr_obj.get_display_name() or rtr_obj.name):
                        continue
                    rtr_info = self._rtr_obj_to_neutron_dict(
                        rtr_obj,
                        contrail_extensions_enabled=extensions_enabled)
                except vnc_exc.NoIdError:
                    continue
                ret_list.append(rtr_info)

        return ret_list

    def resource_count(self, **kwargs):
        filters = kwargs.get('filters')
        count = self._resource_count_optimized(filters)
        if count is not None:
            return count

        rtrs_info = self.router_list(filters=filters)
        return len(rtrs_info)


class LogicalRouterInterfaceHandler(res_handler.ResourceGetHandler,
                                    res_handler.ResourceUpdateHandler,
                                    LogicalRouterMixin):
    resource_get_method = 'logical_router_read'
    resource_list_method = 'logical_routers_list'
    resource_update_method = 'logical_router_update'

    def __init__(self, vnc_lib):
        super(LogicalRouterInterfaceHandler, self).__init__(vnc_lib)
        self._vmi_handler = vmi_handler.VMInterfaceHandler(
            self._vnc_lib)
        self._subnet_handler = subnet_handler.SubnetHandler(self._vnc_lib)

    def _get_subnet_cidr(self, subnet_id, subnet_dict):
        for subnet in subnet_dict:
            if subnet['id'] == subnet_id:
                return subnet['cidr']

    def _check_for_dup_router_subnet(self, router_id, subnet_id,
                                     subnet_cidr):
        try:
            router_vmi_objs = self._vmi_handler._resource_list(
                back_ref_id=[router_id])
            # It's possible router ports are on the same network, but
            # different subnets.
            new_ipnet = netaddr.IPNetwork(subnet_cidr)
            port_req_memo = {'virtual-machines': {},
                             'instance-ips': {},
                             'subnets': {}}
            for vmi_obj in router_vmi_objs:
                net_id = self._vmi_handler.get_vmi_net_id(vmi_obj)
                vn_obj = self._vnc_lib.virtual_network_read(id=net_id)

                fixed_ips = self._vmi_handler.get_vmi_ip_dict(vmi_obj, vn_obj,
                                                              port_req_memo)
                vn_subnets = (
                    subnet_handler.ContrailSubnetHandler.get_vn_subnets(
                        vn_obj))
                for ip in fixed_ips:
                    if ip['subnet_id'] == subnet_id:
                        msg = ("Router %s already has a port on subnet %s"
                               % (router_id, subnet_id))
                        self._raise_contrail_exception(
                            'BadRequest', resource='router', msg=msg)
                    sub_id = ip['subnet_id']
                    cidr = self._get_subnet_cidr(sub_id, vn_subnets)
                    ipnet = netaddr.IPNetwork(cidr)
                    match1 = netaddr.all_matching_cidrs(new_ipnet, [cidr])
                    match2 = netaddr.all_matching_cidrs(ipnet, [subnet_cidr])
                    if match1 or match2:
                        data = {'subnet_cidr': subnet_cidr,
                                'subnet_id': subnet_id,
                                'cidr': cidr,
                                'sub_id': sub_id}
                        msg = (("Cidr %(subnet_cidr)s of subnet "
                                "%(subnet_id)s overlaps with cidr %(cidr)s "
                                "of subnet %(sub_id)s") % data)
                        self._raise_contrail_exception(
                            'BadRequest', resource='router', msg=msg)
        except vnc_exc.NoIdError:
            pass

    def _get_router_iface_vnc_info(self, context, router_id, port_id=None,
                                   subnet_id=None):
        if port_id:
            vmi_obj, vn_obj, rtr_uuid, fixed_ips = self._get_vmi_info(port_id)
            net_id = vn_obj.uuid
            if rtr_uuid:
                self._raise_contrail_exception('PortInUse',
                                               net_id=net_id,
                                               port_id=port_id,
                                               device_id=rtr_uuid)
            if len(fixed_ips) != 1:
                self._raise_contrail_exception(
                    'BadRequest', resource='router',
                    msg='Router port must have exactly one fixed IP')

            subnet_id = fixed_ips[0]['subnet_id']

        subnet_vnc = self._subnet_handler._subnet_read(subnet_id=subnet_id)
        if not subnet_vnc.default_gateway:
            self._raise_contrail_exception(
                'BadRequest', resource='router',
                msg='Subnet for router interface must have a gateway IP')
        subnet_cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                                 subnet_vnc.subnet.get_ip_prefix_len())

        self._check_for_dup_router_subnet(router_id, subnet_id, subnet_cidr)

        if not port_id:
            vn_obj = self._subnet_handler.get_vn_obj_for_subnet_id(subnet_id)
            fixed_ip = {'ip_address': subnet_vnc.default_gateway,
                        'subnet_id': subnet_id}
            port_q = {'tenant_id': vn_obj.parent_uuid.replace('-', ''),
                      'network_id': vn_obj.uuid,
                      'fixed_ips': [fixed_ip],
                      'admin_state_up': True,
                      'device_id': router_id,
                      'device_owner': n_constants.DEVICE_OWNER_ROUTER_INTF,
                      'name': ''}
            port = self._vmi_handler.resource_create(context=context,
                                                     port_q=port_q)
            vmi_obj = self._vmi_handler._resource_get(id=port['id'])

        return vmi_obj, vn_obj, subnet_id

    def _update_snat_routing_table(self, router_obj, network_id,
                                   set_snat=True):
        project_obj = self._vnc_lib.project_read(id=router_obj.parent_uuid)
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.get_fq_name() + [rt_name]

        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            # No route table set with that router ID, the gateway is not set
            return

        try:
            vn_obj = self._vnc_lib.virtual_network_read(id=network_id)
        except vnc_exc.NoIdError:
            raise n_exceptions.NetworkNotFound(net_id=network_id)
        if set_snat:
            vn_obj.set_route_table(rt_obj)
        else:
            vn_obj.del_route_table(rt_obj)
        self._vnc_lib.virtual_network_update(vn_obj)

    def _get_vmi_info(self, port_id):
        vmi_obj = self._vmi_handler._resource_get(id=port_id)
        net_id = self._vmi_handler.get_vmi_net_id(vmi_obj)
        port_req_memo = {'virtual-machines': {},
                         'instance-ips': {},
                         'subnets': {}}
        router_refs = getattr(vmi_obj, 'logical_router_back_refs', None)
        if router_refs:
            rtr_uuid = router_refs[0]['uuid']
        else:
            vm_ref = vmi_obj.get_virtual_machine_refs()
            if vm_ref:
                rtr_uuid = self._vmi_handler.get_port_gw_id(vm_ref[0],
                                                            port_req_memo)
            else:
                rtr_uuid = None

        vn_obj = self._vnc_lib.virtual_network_read(id=net_id)
        fixed_ips = self._vmi_handler.get_vmi_ip_dict(vmi_obj, vn_obj,
                                                      port_req_memo)
        return vmi_obj, vn_obj, rtr_uuid, fixed_ips

    def add_router_interface(self, context, router_id, port_id=None,
                             subnet_id=None):
        router_obj = self._resource_get(id=router_id)

        if not port_id and not subnet_id:
            self._raise_contrail_exception(
                'BadRequest', resource='router',
                msg='Either port or subnet must be specified')

        vmi_obj, vn_obj, subnet_id = self._get_router_iface_vnc_info(
            context, router_id, port_id=port_id, subnet_id=subnet_id)

        self._update_snat_routing_table(router_obj, vn_obj.uuid)
        vmi_obj.set_virtual_machine_interface_device_owner(
            n_constants.DEVICE_OWNER_ROUTER_INTF)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        router_obj.add_virtual_machine_interface(vmi_obj)
        self._resource_update(router_obj)
        info = {'id': router_id,
                'tenant_id': vn_obj.parent_uuid.replace('-', ''),
                'port_id': vmi_obj.uuid,
                'subnet_id': subnet_id}
        return info

    def remove_router_interface(self, router_id, port_id=None,
                                subnet_id=None):
        router_obj = self._resource_get(id=router_id)
        tenant_id = None
        vmi_obj = None
        if port_id:
            vmi_obj, vn_obj, rtr_uuid, fixed_ips = self._get_vmi_info(port_id)
            if not rtr_uuid:
                self._raise_contrail_exception('RouterInterfaceNotFound',
                                               router_id=router_id,
                                               port_id=port_id)
            port_subnet_id = fixed_ips[0]['subnet_id']
            if subnet_id and (port_subnet_id != subnet_id):
                self._raise_contrail_exception('SubnetMismatchForPort',
                                               port_id=port_id,
                                               subnet_id=subnet_id)
            subnet_id = port_subnet_id
        elif subnet_id:
            vn_obj = self._subnet_handler.get_vn_obj_for_subnet_id(subnet_id)

            for intf in router_obj.get_virtual_machine_interface_refs() or []:
                port_id = intf['uuid']
                _, _, _, fixed_ips = self._get_vmi_info(port_id)
                if subnet_id == fixed_ips[0]['subnet_id']:
                    break
            else:
                msg = ("Subnet %s not connected to router %s "
                       % (router_id, subnet_id))
                self._raise_contrail_exception('BadRequest',
                                               resource='router', msg=msg)
        network_id = vn_obj.uuid
        tenant_id = vn_obj.parent_uuid.replace('-', '')
        self._update_snat_routing_table(router_obj, network_id, set_snat=False)
        if not vmi_obj:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=port_id)
        router_obj.del_virtual_machine_interface(vmi_obj)
        self._vnc_lib.logical_router_update(router_obj)
        self._vmi_handler.resource_delete(port_id=port_id)
        info = {'id': router_id,
                'tenant_id': tenant_id,
                'port_id': port_id,
                'subnet_id': subnet_id}
        return info


class LogicalRouterHandler(LogicalRouterGetHandler,
                           LogicalRouterCreateHandler,
                           LogicalRouterDeleteHandler,
                           LogicalRouterUpdateHandler):
    pass
