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
import subnet_res_handler as subnet_handler
import vmi_res_handler as vmi_handler


class VNetworkMixin(object):

    def neutron_dict_to_vn(self, vn_obj, network_q):
        net_name = network_q.get('name')
        if net_name:
            vn_obj.display_name = net_name

        id_perms = vn_obj.get_id_perms()
        if 'admin_state_up' in network_q:
            id_perms.enable = network_q['admin_state_up']
            vn_obj.set_id_perms(id_perms)

        if 'contrail:policys' in network_q:
            policy_fq_names = network_q['contrail:policys']
            # reset and add with newly specified list
            vn_obj.set_network_policy_list([], [])
            seq = 0
            for p_fq_name in policy_fq_names:
                domain_name, project_name, policy_name = p_fq_name

                domain_obj = vnc_api.Domain(domain_name)
                project_obj = vnc_api.Project(project_name, domain_obj)
                policy_obj = vnc_api.NetworkPolicy(policy_name, project_obj)

                vn_obj.add_network_policy(
                    policy_obj,
                    vnc_api.VirtualNetworkPolicyType(
                        sequence=vnc_api.SequenceType(seq, 0)))
                seq = seq + 1

        if 'contrail:route_table' in network_q:
            rt_fq_name = network_q['contrail:route_table']
            if rt_fq_name:
                try:
                    rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
                    vn_obj.set_route_table(rt_obj)
                except vnc_api.NoIdError:
                    # TODO() add route table specific exception
                    db_handler.DBInterfaceV2._raise_contrail_exception(
                        'NetworkNotFound', net_id=vn_obj.uuid)

        return vn_obj

    def _get_vn_extra_dict(self, vn_obj):
        extra_dict = {}
        extra_dict['contrail:fq_name'] = vn_obj.get_fq_name()
        extra_dict['contrail:instance_count'] = 0

        net_policy_refs = vn_obj.get_network_policy_refs()
        if net_policy_refs:
            sorted_refs = sorted(
                net_policy_refs,
                key=lambda t: (t['attr'].sequence.major,
                               t['attr'].sequence.minor))
            extra_dict['contrail:policys'] = [np_ref['to'] for np_ref in
                                              sorted_refs]

        rt_refs = vn_obj.get_route_table_refs()
        if rt_refs:
            extra_dict['contrail:route_table'] = [rt_ref['to'] for rt_ref in
                                                  rt_refs]

        return extra_dict

    def _add_vn_subnet_info(self, vn_obj, net_q_dict, extra_dict=None):
        ipam_refs = vn_obj.get_network_ipam_refs()
        net_q_dict['subnets'] = []
        if not ipam_refs:
            return

        if extra_dict:
            extra_dict['contrail:subnet_ipam'] = []

        sn_handler = subnet_handler.SubnetHandler(self._vnc_lib)
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                sn_id, sn_cidr = sn_handler.get_subnet_id_cidr(subnet, vn_obj)
                net_q_dict['subnets'].append(sn_id)

                if not extra_dict:
                    continue

                sn_ipam = {}
                sn_ipam['subnet_cidr'] = sn_cidr
                sn_ipam['ipam_fq_name'] = ipam_ref['to']
                extra_dict['contrail:subnet_ipam'].append(sn_ipam)

    def vn_to_neutron_dict(self, vn_obj, contrail_extensions_enabled=False):
        net_q_dict = {}
        extra_dict = None

        id_perms = vn_obj.get_id_perms()
        net_q_dict['id'] = vn_obj.uuid

        if not vn_obj.display_name:
            # for nets created directly via vnc_api
            net_q_dict['name'] = vn_obj.get_fq_name()[-1]
        else:
            net_q_dict['name'] = vn_obj.display_name

        net_q_dict['tenant_id'] = vn_obj.parent_uuid.replace('-', '')
        net_q_dict['admin_state_up'] = id_perms.enable
        net_q_dict['shared'] = True if vn_obj.is_shared else False
        net_q_dict['status'] = (n_constants.NET_STATUS_ACTIVE
                                if id_perms.enable
                                else n_constants.NET_STATUS_DOWN)
        net_q_dict['router:external'] = (True if vn_obj.router_external
                                         else False)
        if contrail_extensions_enabled:
            extra_dict = self._get_vn_extra_dict(vn_obj)

        self._add_vn_subnet_info(vn_obj, net_q_dict, extra_dict)

        if contrail_extensions_enabled:
            net_q_dict.update(extra_dict)

        return net_q_dict

    def get_vn_tenant_id(self, vn_obj):
        return vn_obj.parent_uuid.replace('-', '')

    def _network_vnc_to_neutron(self, net_obj, net_repr='SHOW',
                                contrail_extensions_enabled=False):
        net_q_dict = {}
        extra_dict = {}

        id_perms = net_obj.get_id_perms()
        net_q_dict['id'] = net_obj.uuid

        if not net_obj.display_name:
            # for nets created directly via vnc_api
            net_q_dict['name'] = net_obj.get_fq_name()[-1]
        else:
            net_q_dict['name'] = net_obj.display_name

        extra_dict['contrail:fq_name'] = net_obj.get_fq_name()
        net_q_dict['tenant_id'] = net_obj.parent_uuid.replace('-', '')
        net_q_dict['admin_state_up'] = id_perms.enable
        if net_obj.is_shared:
            net_q_dict['shared'] = True
        else:
            net_q_dict['shared'] = False
        net_q_dict['status'] = (n_constants.NET_STATUS_ACTIVE
                                if id_perms.enable
                                else n_constants.NET_STATUS_DOWN)
        if net_obj.router_external:
            net_q_dict['router:external'] = True
        else:
            net_q_dict['router:external'] = False

        if net_repr == 'SHOW' or net_repr == 'LIST':
            extra_dict['contrail:instance_count'] = 0

            net_policy_refs = net_obj.get_network_policy_refs()
            if net_policy_refs:
                sorted_refs = sorted(
                    net_policy_refs,
                    key=lambda t: (t['attr'].sequence.major,
                                   t['attr'].sequence.minor))
                extra_dict['contrail:policys'] = (
                    [np_ref['to'] for np_ref in sorted_refs])

        rt_refs = net_obj.get_route_table_refs()
        if rt_refs:
            extra_dict['contrail:route_table'] = (
                [rt_ref['to'] for rt_ref in rt_refs])

        ipam_refs = net_obj.get_network_ipam_refs()
        net_q_dict['subnets'] = []
        if ipam_refs:
            extra_dict['contrail:subnet_ipam'] = []
            sn_handler = subnet_handler.SubnetHandler(self._vnc_lib)
            for ipam_ref in ipam_refs:
                subnets = ipam_ref['attr'].get_ipam_subnets()
                for subnet in subnets:
                    sn_dict = sn_handler._subnet_vnc_to_neutron(
                        subnet, net_obj, ipam_ref['to'])
                    net_q_dict['subnets'].append(sn_dict['id'])
                    sn_ipam = {}
                    sn_ipam['subnet_cidr'] = sn_dict['cidr']
                    sn_ipam['ipam_fq_name'] = ipam_ref['to']
                    extra_dict['contrail:subnet_ipam'].append(sn_ipam)

        if contrail_extensions_enabled:
            net_q_dict.update(extra_dict)

        return net_q_dict
    # end _network_vnc_to_neutron


class VNetworkCreateHandler(res_handler.ResourceCreateHandler, VNetworkMixin):
    resource_create_method = 'virtual_network_create'

    def create_vn_obj(self, network_q):
        if 'tenant_id' not in network_q:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest', resource='virtual_network',
                msg="'tenant_id' is mandator")
        net_name = network_q.get('name', None)
        project_id = str(uuid.UUID(network_q['tenant_id']))
        try:
            proj_obj = self._project_read(proj_id=project_id)
        except vnc_exc.NoIdError:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'ProjectNotFound', project_id=project_id)
        id_perms = vnc_api.IdPermsType(enable=True)
        vn_obj = vnc_api.VirtualNetwork(net_name, proj_obj,
                                        id_perms=id_perms)
        external_attr = network_q.get('router:external')
        if external_attr is not None:
            vn_obj.router_external = external_attr
        else:
            vn_obj.router_external = False

        is_shared = network_q.get('shared')
        if is_shared is not None:
            vn_obj.is_shared = is_shared
        else:
            vn_obj.is_shared = False

        return vn_obj

    def resource_create(self, network_q, contrail_extensions_enabled=False):
        vn_obj = self.neutron_dict_to_vn(self.create_vn_obj(network_q),
                                         network_q)
        self._resource_create(vn_obj)

        if vn_obj.router_external:
            fip_pool_obj = vnc_api.FloatingIpPool('floating-ip-pool', vn_obj)
            self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        ret_network_q = self.vn_to_neutron_dict(
            vn_obj, contrail_extensions_enabled=contrail_extensions_enabled)

        return ret_network_q


class VNetworkUpdateHandler(res_handler.ResourceUpdateHandler, VNetworkMixin):
    resource_update_method = 'virtual_network_update'

    def _update_external_router_attr(self, router_external, vn_obj):
        if router_external and not vn_obj.router_external:
            fip_pool_obj = vnc_api.FloatingIpPool('floating-ip-pool',
                                                  vn_obj)
            self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        else:
            fip_pools = vn_obj.get_floating_ip_pools()
            for fip_pool in fip_pools or []:
                try:
                    self._vnc_lib.floating_ip_pool_delete(id=fip_pool['uuid'])
                except vnc_api.RefsExistError:
                    db_handler.DBInterfaceV2._raise_contrail_exception(
                        'NetworkInUse', net_id=vn_obj.uuid)

    def _validate_shared_attr(self, is_shared, vn_obj):
        if is_shared and not vn_obj.is_shared:
            for vmi in vn_obj.get_virtual_machine_interface_back_refs() or []:
                vmi_obj = vmi_handler.VMInterfaceHandler(
                    self._vnc_lib)._resource_get(
                    id=vmi['uuid'])
                if vmi_obj.parent_type == 'project' and (
                   vmi_obj.parent_uuid != vn_obj.parent_uuid):
                    db_handler.DBInterfaceV2._raise_contrail_exception(
                        'InvalidSharedSetting',
                        network=vn_obj.display_name)

    def get_vn_obj(self, network_q):
        try:
            vn_obj = self._resource_get(id=network_q['id'])
        except vnc_exc.NoIdError:
            raise db_handler.DBInterfaceV2._raise_contrail_exception(
                'NetwrokNotFound',
                net_id=network_q['id'])
        router_external = network_q.get('router:external')
        if router_external is not None:
            if router_external != vn_obj.router_external:
                self._update_external_router_attr(router_external, vn_obj)
                vn_obj.router_external = router_external

        is_shared = network_q.get('shared')
        if is_shared is not None:
            if is_shared != vn_obj.is_shared:
                self._validate_shared_attr(is_shared, vn_obj)
                vn_obj.is_shared = is_shared

        return vn_obj

    def resource_update(self, net_id, network_q,
                        contrail_extensions_enabled=True):
        network_q['id'] = net_id
        vn_obj = self.neutron_dict_to_vn(self.get_vn_obj(network_q), network_q)
        self._resource_update(vn_obj)

        ret_network_q = self.vn_to_neutron_dict(
            vn_obj, contrail_extensions_enabled=contrail_extensions_enabled)

        return ret_network_q


class VNetworkGetHandler(res_handler.ResourceGetHandler, VNetworkMixin):
    resource_list_method = 'virtual_networks_list'
    resource_get_method = 'virtual_network_read'
    detail = False

    def _network_list_project(self, project_id, count=False):
        if project_id:
            try:
                project_uuid = str(uuid.UUID(project_id))
            except Exception:
                print("Error in converting uuid %s" % (project_id))
        else:
            project_uuid = None

        if count:
            ret_val = self._resource_list(parent_id=project_uuid,
                                          count=True)
        else:
            ret_val = self._resource_list(parent_id=project_uuid,
                                          detail=True)

        return ret_val
    # end _network_list_project

    def _network_list_shared_and_ext(self):
        ret_list = []
        nets = self._network_list_project(project_id=None)
        for net in nets:
            if net.get_router_external() and net.get_is_shared():
                ret_list.append(net)
        return ret_list
    # end _network_list_router_external

    def _network_list_router_external(self):
        ret_list = []
        nets = self._network_list_project(project_id=None)
        for net in nets:
            if not net.get_router_external():
                continue
            ret_list.append(net)
        return ret_list
    # end _network_list_router_external

    def _network_list_shared(self):
        ret_list = []
        nets = self._network_list_project(project_id=None)
        for net in nets:
            if not net.get_is_shared():
                continue
            ret_list.append(net)
        return ret_list
    # end _network_list_shared

    def resource_list(self, context, filters,
                      contrail_extensions_enabled=False):
        contrail_exts_enabled = contrail_extensions_enabled
        ret_dict = {}

        def _collect_without_prune(net_ids):
            for net_id in net_ids:
                try:
                    net_obj = self._resource_get(id=net_id)
                    net_info = self._network_vnc_to_neutron(
                        net_obj,
                        net_repr='LIST',
                        contrail_extensions_enabled=contrail_exts_enabled)
                    ret_dict[net_id] = net_info
                except vnc_exc.NoIdError:
                    pass
        # end _collect_without_prune

        # collect phase
        all_net_objs = []  # all n/ws in all projects
        if context and not context['is_admin']:
            if filters and 'id' in filters:
                _collect_without_prune(filters['id'])
            elif filters and 'name' in filters:
                net_objs = self._network_list_project(context['tenant'])
                all_net_objs.extend(net_objs)
                all_net_objs.extend(self._network_list_shared())
                all_net_objs.extend(self._network_list_router_external())
            elif (filters and 'shared' in filters and filters['shared'][0] and
                  'router:external' not in filters):
                all_net_objs.extend(self._network_list_shared())
            elif (filters and 'router:external' in filters and
                  'shared' not in filters):
                all_net_objs.extend(self._network_list_router_external())
            elif (filters and 'router:external' in filters and
                  'shared' in filters):
                all_net_objs.extend(self._network_list_shared_and_ext())
            else:
                project_uuid = str(uuid.UUID(context['tenant']))
                if not filters:
                    all_net_objs.extend(self._network_list_router_external())
                    all_net_objs.extend(self._network_list_shared())
                all_net_objs.extend(self._network_list_project(project_uuid))
        # admin role from here on
        elif filters and 'tenant_id' in filters:
            # project-id is present
            if 'id' in filters:
                # required networks are also specified,
                # just read and populate ret_dict
                # prune is skipped because all_net_objs is empty
                _collect_without_prune(filters['id'])
            else:
                # read all networks in project, and prune below
                proj_ids = db_handler.DBInterfaceV2._validate_project_ids(
                    context, filters['tenant_id'])
                for p_id in proj_ids:
                    all_net_objs.extend(self._network_list_project(p_id))
                if 'router:external' in filters:
                    all_net_objs.extend(self._network_list_router_external())
        elif filters and 'id' in filters:
            # required networks are specified, just read and populate ret_dict
            # prune is skipped because all_net_objs is empty
            _collect_without_prune(filters['id'])
        elif filters and 'name' in filters:
            net_objs = self._network_list_project(None)
            all_net_objs.extend(net_objs)
        elif filters and 'shared' in filters:
            if filters['shared'][0]:
                nets = self._network_list_shared()
                for net in nets:
                    net_info = self._network_vnc_to_neutron(net,
                                                            net_repr='LIST')
                    ret_dict[net.uuid] = net_info
        elif filters and 'router:external' in filters:
            nets = self._network_list_router_external()
            if filters['router:external'][0]:
                for net in nets:
                    net_info = self._network_vnc_to_neutron(net,
                                                            net_repr='LIST')
                    ret_dict[net.uuid] = net_info
        else:
            # read all networks in all projects
            all_net_objs.extend(self._resource_list(detail=True))

        # prune phase
        for net_obj in all_net_objs:
            if net_obj.uuid in ret_dict:
                continue
            net_fq_name = unicode(net_obj.get_fq_name())
            if not db_handler.DBInterfaceV2._filters_is_present(
                    filters, 'contrail:fq_name', net_fq_name):
                continue
            if not db_handler.DBInterfaceV2._filters_is_present(
                    filters, 'name',
                    net_obj.get_display_name() or net_obj.name):
                continue
            if net_obj.is_shared is None:
                is_shared = False
            else:
                is_shared = net_obj.is_shared
            if not db_handler.DBInterfaceV2._filters_is_present(
                    filters, 'shared', is_shared):
                continue
            try:
                net_info = self._network_vnc_to_neutron(net_obj,
                                                        net_repr='LIST')
            except vnc_exc.NoIdError:
                continue
            ret_dict[net_obj.uuid] = net_info
        ret_list = []
        for net in ret_dict.values():
            ret_list.append(net)

        return ret_list

    def resource_get(self, net_uuid, contrail_extensions_enabled=True):
        try:
            vn_obj = self._resource_get(id=net_uuid)
        except vnc_exc.NoIdError:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'NetworkNotFound', net_id=net_uuid)

        return self.vn_to_neutron_dict(
            vn_obj, contrail_extensions_enabled)

    def resource_count(self, filters):
        count = self._resource_count_optimized(filters)
        if count is not None:
            return count

        nets_info = self.resource_list(context=None, filters=filters)
        return len(nets_info)

    def get_vn_list_project(self, project_id, count=False):
        if project_id:
            try:
                project_uuid = str(uuid.UUID(project_id))
            except ValueError:
                project_uuid = None
        else:
            project_uuid = None

        if count:
            ret_val = self._resource_list(parent_id=project_uuid,
                                          count=True)
        else:
            ret_val = self._resource_list(parent_id=project_uuid,
                                          detail=True)

        return ret_val

    def vn_list_shared(self):
        ret_list = []
        nets = self.get_vn_list_project(project_id=None)
        for net in nets:
            if not net.get_is_shared():
                continue
            ret_list.append(net)
        return ret_list


class VNetworkDeleteHandler(res_handler.ResourceDeleteHandler):
    resource_delete_method = 'virtual_network_delete'

    def resource_delete(self, net_id):
        try:
            vn_obj = self._resource_get(id=net_id)
        except vnc_api.NoIdError:
            return

        try:
            fip_pools = vn_obj.get_floating_ip_pools()
            for fip_pool in fip_pools or []:
                self._vnc_lib.floating_ip_pool_delete(id=fip_pool['uuid'])

            self._resource_delete(id=net_id)
        except vnc_api.RefsExistError:
            db_handler.DBInterfaceV2._raise_contrail_exception('NetworkInUse',
                                                               net_id=net_id)


class VNetworkHandler(VNetworkGetHandler,
                      VNetworkCreateHandler,
                      VNetworkUpdateHandler,
                      VNetworkDeleteHandler):
    pass
