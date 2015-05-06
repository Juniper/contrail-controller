# Copyright (c) 2015 Juniper Networks, Inc.
# All Rights Reserved.
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
#

import uuid

from vnc_api.vnc_api import *

from cfgm_common import importutils
from cfgm_common import exceptions as vnc_exc
from cfgm_common import svc_info

from agent import Agent
from config_db import VirtualNetworkSM

SNAT_SERVICE_TEMPLATE_FQ_NAME = ['default-domain', 'netns-snat-template']


class SnatAgent(Agent):

    # service types handled by this agent
    _service_types = [svc_info.get_snat_service_type()]

    def __init__(self, svc_mon, vnc_lib, args):
        super(SnatAgent, self).__init__(svc_mon, vnc_lib, args)

    def handle_service_type(self, type):
        return type in self._service_types

    def pre_create_service_vm(self, instance_index, si, st, vm):
        for nic in si.vn_info:
            if nic['type'] == svc_info.get_left_if_str():
                nic['user-visible'] = False

    def _create_snat_vn(self, vn_name, vn_subnet, proj_obj):
        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj)
        id_perms = IdPermsType(enable=True, user_visible=False)
        vn_obj.set_id_perms(id_perms)
        domain_name, project_name = proj_obj.get_fq_name()
        ipam_fq_name = [domain_name, project_name, 'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        cidr = vn_subnet.split('/')
        pfx = cidr[0]
        pfx_len = int(cidr[1])
        subnet_info = IpamSubnetType(subnet=SubnetType(pfx, pfx_len))
        subnet_data = VnSubnetsType([subnet_info])
        vn_obj.add_network_ipam(ipam_obj, subnet_data)
        self._vnc_lib.virtual_network_create(vn_obj)
        VirtualNetworkSM.locate(vn_obj.uuid)

        return vn_obj.uuid

    def _get_snat_vn(self, proj_obj, si_obj):
        vn_name = '%s_%s' % (svc_info.get_snat_left_vn_prefix(),
                             si_obj.name)
        vn_fq_name = si_obj.fq_name[:-1] + [vn_name]
        try:
            vn_id = self._vnc_lib.fq_name_to_id(
                'virtual-network', vn_fq_name)
        except NoIdError:
            snat_subnet = svc_info.get_snat_left_subnet()
            self._create_snat_vn(vn_name, snat_subnet, proj_obj)

        return ':'.join(vn_fq_name)

    def update_gateway(self, router, router_dict):
        if (not (router_dict.get('virtual_network_refs') and
                router_dict.get('virtual_machine_interface_refs')) and
                router_dict.get('service_instance_refs')):
            return self.clear_gateway(router, router_dict)
        elif (router_dict.get('virtual_network_refs') and
                 router_dict.get('virtual_machine_interface_refs')):
            if router.last_sent:
                vn_uuid = router_dict['virtual_network_refs'][0]['uuid']
                old_vn_uuid = router.last_sent['virtual_network_refs'][0]['uuid']
                if vn_uuid == old_vn_uuid:
                    return router.last_sent

            return self.set_gateway(router, router_dict)

    def set_gateway(self, router, router_dict):
        vn_uuid = router_dict['virtual_network_refs'][0]['uuid']

        ext_net_obj = self._vnc_lib.virtual_network_read(id=vn_uuid)
        if not ext_net_obj.get_router_external():
            return

        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=SNAT_SERVICE_TEMPLATE_FQ_NAME)
        except NoIdError:
            # TODO(safchain) raise something here
            raise

        si_obj = None
        if (router_dict.get('service_instance_refs') and
               router_dict['service_instance_refs'][0]['uuid']):
            try:
                si_obj = self._vnc_lib.service_instance_read(
                    id=router_dict['service_instance_refs'][0]['uuid'])
            except NoIdError:
                pass

        proj_obj = self._vnc_lib.project_read(id=router_dict['parent_uuid'])

        # Set the service instance
        si_created = False
        if not si_obj:
            si_name = 'snat_' + router_dict['uuid'] + '_' + str(uuid.uuid4())
            si_obj = ServiceInstance(si_name, parent_obj=proj_obj)
            si_created = True

        si_prop_obj = ServiceInstanceType(
            scale_out=ServiceScaleOutType(max_instances=2,
                                          auto_scale=True),
            auto_policy=True)

	rt_name = 'rt_' + router_dict['uuid']
        rt_fq_name = proj_obj.get_fq_name() + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
            rt_uuid = rt_obj.uuid
        except NoIdError:
            rt_obj = None

        # get or create the left snat network
        vn_left_fq_name = self._get_snat_vn(proj_obj, si_obj)

        # set right interface in order of [right, left] to match template
        left_if = ServiceInstanceInterfaceType(virtual_network=vn_left_fq_name)

        # right network, should be the public one
        right_if = ServiceInstanceInterfaceType(
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
        route_obj = RouteType(prefix="0.0.0.0/0",
                              next_hop=si_obj.get_fq_name_str())
        rt_created = False
        if not rt_obj:
            rt_obj = RouteTable(name=rt_name, parent_obj=proj_obj)
            rt_created = True
        rt_obj.set_routes(RouteTableType.factory([route_obj]))
        if rt_created:
            rt_uuid = self._vnc_lib.route_table_create(rt_obj)
        else:
            self._vnc_lib.route_table_update(rt_obj)

        # Associate route table to all private networks connected onto
        # that router
        for intf in router_dict.get('virtual_machine_interface_refs', []):
            try:
                port_obj = self._vnc_lib.virtual_machine_interface_read(
                    id=intf['uuid'])
            except NoIdError:
                # TODO(safchain raise something here
                raise

            net_id = port_obj.get_virtual_network_refs()[0]['uuid']
            try:
                net_obj = self._vnc_lib.virtual_network_read(id=net_id)
            except NoIdError:
                # TODO(safchain raise something here
                raise
            net_obj.set_route_table(rt_obj)
            self._vnc_lib.virtual_network_update(net_obj)

        # Add logical gateway virtual network
        router_obj = LogicalRouter()
        router_obj.uuid = router_dict['uuid']
        router_obj.fq_name = router_dict['fq_name']
        router_obj.set_service_instance(si_obj)
        self._vnc_lib.logical_router_update(router_obj)

        return router_dict

    def clear_gateway(self, router, router_dict):
        proj_obj = self._vnc_lib.project_read(id=router_dict['parent_uuid'])

        # Get route table for default route it it exists
        rt_name = 'rt_' + router_dict['uuid']
        rt_fq_name = proj_obj.get_fq_name() + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
            rt_uuid = rt_obj.uuid
        except NoIdError:
            rt_obj = None

        # Delete route table
        if rt_obj:
            # Disassociate route table to all private networks connected
            # onto that router
            for net_ref in rt_obj.get_virtual_network_back_refs() or []:
                try:
                    net_obj = self._vnc_lib.virtual_network_read(
                        id=net_ref['uuid'])
                except NoIdError:
                    continue
                net_obj.del_route_table(rt_obj)
                self._vnc_lib.virtual_network_update(net_obj)
            self._vnc_lib.route_table_delete(id=rt_obj.uuid)

        # Clear logical gateway virtual network
        router_obj = self._vnc_lib.logical_router_read(id=router_dict['uuid'])
        router_obj.set_virtual_network_list([])
        router_obj.set_service_instance_list([])
        self._vnc_lib.logical_router_update(router_obj)

        si_uuid = router_dict['service_instance_refs'][0]['uuid']
        self._vnc_lib.service_instance_delete(id=si_uuid)
