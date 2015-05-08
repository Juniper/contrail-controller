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


try:
    import ujson as json
except ImportError:
    import json
import uuid

import bottle

import fip_res_handler as fip_handler
import ipam_res_handler as ipam_handler
from neutron_plugin_db import DBInterface
import policy_res_handler as policy_handler
import route_table_res_handler as route_table_handler
import router_res_handler as rtr_handler
import sg_res_handler as sg_handler
import sgrule_res_handler as sgrule_handler
import subnet_res_handler as subnet_handler
import svc_instance_res_handler as svc_instance_handler
import vmi_res_handler as vmi_handler
import vn_res_handler as vn_handler


class DBInterfaceV2(DBInterface):

    def __init__(self, *args, **kwargs):
        super(DBInterfaceV2, self).__init__(*args, **kwargs)

    # Encode and send an excption information to neutron. exc must be a
    # valid exception class name in neutron, kwargs must contain all
    # necessary arguments to create that exception
    @staticmethod
    def _raise_contrail_exception(exc, **kwargs):
        exc_info = {'exception': exc}
        exc_info.update(kwargs)
        bottle.abort(400, json.dumps(exc_info))

    @staticmethod
    def _validate_project_ids(context, project_ids=None):
        if context and not context['is_admin']:
            return [context['tenant']]

        ids = []
        for project_id in project_ids:
            try:
                ids.append(str(uuid.UUID(project_id)))
            except ValueError:
                pass
        return ids

    @staticmethod
    def _filters_is_present(filters, key_name, match_value):
        if filters:
            if key_name in filters:
                try:
                    if key_name == 'tenant_id':
                        filter_value = [t_id for t_id in filters[key_name]]
                    else:
                        filter_value = filters[key_name]
                    filter_value.index(match_value)
                except ValueError:  # not in requested list
                    return False
        return True

    def network_create(self, network_q):
        handler = vn_handler.VNetworkHandler(self._vnc_lib)
        return handler.resource_create(
            network_q=network_q,
            contrail_extensions_enabled=self._contrail_extensions_enabled)

    def network_update(self, net_id, network_q):
        handler = vn_handler.VNetworkHandler(self._vnc_lib)
        return handler.resource_update(net_id=net_id,
                                       network_q=network_q)

    def network_delete(self, net_id):
        handler = vn_handler.VNetworkHandler(self._vnc_lib)
        handler.resource_delete(net_id=net_id)

    def network_read(self, net_uuid, fields=None):
        handler = vn_handler.VNetworkHandler(self._vnc_lib)
        return handler.resource_get(
            net_uuid=net_uuid,
            contrail_extensions_enabled=self._contrail_extensions_enabled)

    def network_list(self, context=None, filters=None):
        handler = vn_handler.VNetworkHandler(self._vnc_lib)
        return handler.resource_list(
            context=context, filters=filters,
            contrail_extensions_enabled=self._contrail_extensions_enabled)

    def network_count(self, filters=None):
        handler = vn_handler.VNetworkHandler(self._vnc_lib)
        return handler.resource_count(filters=filters)

    def port_create(self, context, port_q):
        handler = vmi_handler.VMInterfaceHandler(
            self._vnc_lib)
        return handler.resource_create(
            context=context, port_q=port_q,
            apply_subnet_host_routes=self._apply_subnet_host_routes)

    def port_delete(self, port_id):
        handler = vmi_handler.VMInterfaceHandler(
            self._vnc_lib)
        handler.resource_delete(port_id=port_id)

    def port_update(self, port_id, port_q):
        handler = vmi_handler.VMInterfaceHandler(
            self._vnc_lib)
        return handler.resource_update(port_id=port_id,
                                       port_q=port_q)

    def port_read(self, port_id):
        handler = vmi_handler.VMInterfaceHandler(self._vnc_lib)
        return handler.resource_get(port_id=port_id)

    def port_list(self, context=None, filters=None):
        handler = vmi_handler.VMInterfaceHandler(self._vnc_lib)
        return handler.resource_list(context=context, filters=filters)

    def port_count(self, filters=None):
        handler = vmi_handler.VMInterfaceHandler(self._vnc_lib)
        return handler.resource_count(filters=filters)

    def subnet_create(self, subnet_q):
        handler = subnet_handler.SubnetHandler(self._vnc_lib)
        return handler.resource_create(subnet_q=subnet_q)

    def subnet_update(self, subnet_id, subnet_q):
        handler = subnet_handler.SubnetHandler(self._vnc_lib)
        return handler.resource_update(
            subnet_id=subnet_id, subnet_q=subnet_q,
            apply_subnet_host_routes=self._apply_subnet_host_routes)

    def subnet_delete(self, subnet_id):
        sn_delete_handler = subnet_handler.SubnetHandler(self._vnc_lib)
        sn_delete_handler.resource_delete(subnet_id=subnet_id)

    def subnet_read(self, subnet_id):
        handler = subnet_handler.SubnetHandler(self._vnc_lib)
        return handler.resource_get(subnet_id=subnet_id)

    def subnets_list(self, context, filters=None):
        handler = subnet_handler.SubnetHandler(self._vnc_lib)
        return handler.resource_list(context=context,
                                     filters=filters)

    def subnets_count(self, context, filters=None):
        handler = subnet_handler.SubnetHandler(self._vnc_lib)
        return handler.resource_count(context=context,
                                      filters=filters)

    def floatingip_create(self, context, fip_q):
        handler = fip_handler.FloatingIpHandler(self._vnc_lib)
        return handler.resource_create(context=context,
                                       fip_q=fip_q)

    def floatingip_delete(self, fip_id):
        handler = fip_handler.FloatingIpHandler(self._vnc_lib)
        handler.resource_delete(fip_id=fip_id)

    def floatingip_update(self, context, fip_id, fip_q):
        handler = fip_handler.FloatingIpHandler(self._vnc_lib)
        return handler.resource_update(context=context,
                                       fip_id=fip_id,
                                       fip_q=fip_q)

    def floatingip_read(self, fip_uuid):
        handler = fip_handler.FloatingIpHandler(self._vnc_lib)
        return handler.resource_get(fip_uuid=fip_uuid)

    def floatingip_list(self, context, filters=None):
        handler = fip_handler.FloatingIpHandler(self._vnc_lib)
        return handler.resource_list(context=context,
                                     filters=filters)

    def floatingip_count(self, context, filters=None):
        handler = fip_handler.FloatingIpHandler(self._vnc_lib)
        return handler.resource_count(context=context,
                                      filters=filters)

    def router_create(self, router_q):
        handler = rtr_handler.LogicalRouterHandler(
            self._vnc_lib)
        return handler.resource_create(router_q=router_q)

    def router_delete(self, rtr_id):
        handler = rtr_handler.LogicalRouterHandler(
            self._vnc_lib)
        return handler.resource_delete(rtr_id=rtr_id)

    def router_update(self, rtr_id, router_q):
        handler = rtr_handler.LogicalRouterHandler(
            self._vnc_lib)
        return handler.resource_update(rtr_id=rtr_id,
                                       router_q=router_q)

    def router_read(self, rtr_uuid, fields=None):
        handler = rtr_handler.LogicalRouterHandler(self._vnc_lib)
        return handler.resource_get(rtr_uuid=rtr_uuid, fields=fields)

    def router_list(self, context=None, filters=None):
        handler = rtr_handler.LogicalRouterHandler(self._vnc_lib)
        return handler.resource_list(context=context, filters=filters)

    def router_count(self, filters=None):
        handler = rtr_handler.LogicalRouterHandler(self._vnc_lib)
        return handler.resource_count(filters=filters)

    def add_router_interface(self, context, router_id, port_id=None,
                             subnet_id=None):
        handler = rtr_handler.LogicalRouterInterfaceHandler(self._vnc_lib)
        return handler.add_router_interface(context=context,
                                            router_id=router_id,
                                            port_id=port_id,
                                            subnet_id=subnet_id)

    def remove_router_interface(self, router_id, port_id=None, subnet_id=None):
        handler = rtr_handler.LogicalRouterInterfaceHandler(self._vnc_lib)
        return handler.remove_router_interface(router_id=router_id,
                                               port_id=port_id,
                                               subnet_id=subnet_id)

    def security_group_create(self, sg_q):
        handler = sg_handler.SecurityGroupHandler(self._vnc_lib)
        return handler.resource_create(
            sg_q=sg_q,
            contrail_extensions_enabled=self.contrail_extensions_enabled)

    def security_group_read(self, sg_id):
        handler = sg_handler.SecurityGroupHandler(self._vnc_lib)
        return handler.resource_get(sg_id=sg_id)

    def security_group_list(self, context, filters=None):
        handler = sg_handler.SecurityGroupHandler(self._vnc_lib)
        return handler.resource_list(
            context, filters=filters,
            contrail_extensions_enabled=self.contrail_extensions_enabled)

    def security_group_delete(self, context, sg_id):
        handler = sg_handler.SecurityGroupHandler(self._vnc_lib)
        return handler.resource_delete(context, sg_id=sg_id)

    def security_group_update(self, sg_id, sg_q):
        handler = sg_handler.SecurityGroupHandler(self._vnc_lib)
        return handler.resource_update(sg_id, sg_q)

    def security_group_rule_read(self, context, sgr_id):
        handler = sgrule_handler.SecurityGroupRuleHandler(self._vnc_lib)
        return handler.resource_get(context, sgr_id)

    def security_group_rule_list(self, context, filters=None):
        handler = sgrule_handler.SecurityGroupRuleHandler(self._vnc_lib)
        return handler.resource_list(context, filters)

    def security_group_rule_delete(self, context, sg_rule):
        handler = sgrule_handler.SecurityGroupRuleHandler(self._vnc_lib)
        return handler.resource_delete(context, sgr_id=sg_rule)

    def security_group_rule_create(self, sgr_q):
        handler = sgrule_handler.SecurityGroupRuleHandler(self._vnc_lib)
        return handler.resource_create(sgr_q)

    def ipam_read(self, ipam_id):
        handler = ipam_handler.IPamHandler(self._vnc_lib)
        return handler.resource_get(ipam_id)

    def ipam_create(self, ipam_q):
        handler = ipam_handler.IPamHandler(self._vnc_lib)
        return handler.resource_create(ipam_q)

    def ipam_delete(self, ipam_id):
        handler = ipam_handler.IPamHandler(self._vnc_lib)
        return handler.resource_delete(ipam_id)

    def ipam_list(self, context=None, filters=None):
        handler = ipam_handler.IPamHandler(self._vnc_lib)
        return handler.resource_list(context, filters)

    def ipam_update(self, ipam_id, ipam_q):
        handler = ipam_handler.IPamHandler(self._vnc_lib)
        return handler.resource_update(ipam_id, ipam_q)

    def ipam_count(self, context, filters=None):
        handler = ipam_handler.IPamHandler(self._vnc_lib)
        return handler.resource_count(filters)

    def policy_create(self, policy_q):
        handler = policy_handler.PolicyHandler(self._vnc_lib)
        return handler.resource_create(policy_q)

    def policy_list(self, context=None, filters=None):
        handler = policy_handler.PolicyHandler(self._vnc_lib)
        return handler.resource_list(context, filters)

    def policy_update(self, policy_id, policy):
        handler = policy_handler.PolicyHandler(self._vnc_lib)
        return handler.resource_update(policy_id, policy)

    def policy_delete(self, policy_id):
        handler = policy_handler.PolicyHandler(self._vnc_lib)
        return handler.resource_delete(policy_id)

    def policy_read(self, policy_id):
        handler = policy_handler.PolicyHandler(self._vnc_lib)
        return handler.resource_get(policy_id)

    def policy_count(self, context=None, filters=None):
        handler = policy_handler.PolicyHandler(self._vnc_lib)
        return handler.resource_count(context, filters)

    def svc_instance_create(self, si_q):
        handler = svc_instance_handler.SvcInstanceHandler(self._vnc_lib)
        return handler.resource_create(si_q)

    def svc_instance_read(self, si_id):
        handler = svc_instance_handler.SvcInstanceHandler(self._vnc_lib)
        return handler.resource_get(si_id)

    def svc_instance_list(self, context, filters=None):
        handler = svc_instance_handler.SvcInstanceHandler(self._vnc_lib)
        return handler.resource_list(context, filters)

    def svc_instance_delete(self, si_id):
        handler = svc_instance_handler.SvcInstanceHandler(self._vnc_lib)
        return handler.resource_delete(si_id)

    def route_table_create(self, rt_q):
        handler = route_table_handler.RouteTableHandler(self._vnc_lib)
        return handler.resource_create(rt_q)

    def route_table_read(self, rt_id):
        handler = route_table_handler.RouteTableHandler(self._vnc_lib)
        return handler.resource_get(rt_id)

    def route_table_list(self, context, filters=None):
        handler = route_table_handler.RouteTableHandler(self._vnc_lib)
        return handler.resource_list(context, filters)

    def route_table_update(self, rt_id, rt_q):
        handler = route_table_handler.RouteTableHandler(self._vnc_lib)
        return handler.resource_update(rt_id, rt_q)

    def route_table_delete(self, rt_id):
        handler = route_table_handler.RouteTableHandler(self._vnc_lib)
        return handler.resource_delete(rt_id)
