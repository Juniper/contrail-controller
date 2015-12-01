# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright (c) 2014 Cloudwatt
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
# @author: Rudra Rugge

from vnc_api.vnc_api import *
from config_db import *
from agent import Agent

class PortTupleAgent(Agent):

    def __init__(self, svc_mon, vnc_lib, cassandra, config_section, logger):
        super(PortTupleAgent, self).__init__(svc_mon, vnc_lib,
            cassandra, config_section)
        self._logger = logger

    def handle_service_type(self):
        return 'port-tuple'

    def _allocate_iip_for_family(self, vn_obj, iip_family, si, port):
        iip_name = si.uuid + '-' + port['type'] + '-' + iip_family
        iip_obj = InstanceIp(name=iip_name, instance_ip_family=iip_family)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.set_service_instance_ip(True)
        iip_obj.set_instance_ip_secondary(True)
        for iip in InstanceIpSM.values():
            if iip.name == iip_name:
                iip_obj.uuid = iip.uuid
                return iip_obj

        if not iip_obj.uuid:
            try:
                self._vnc_lib.instance_ip_create(iip_obj)
            except RefsExistError:
                self._vnc_lib.instance_ip_update(iip_obj)
            except HttpError:
                return None

        tag = ServiceInterfaceTag(interface_type=port['type'])
        self._vnc_lib.ref_update('service-instance', si.uuid,
            'instance-ip', iip_obj.uuid, None, 'ADD', tag)
        InstanceIpSM.locate(iip_obj.uuid)
        return iip_obj

    def _allocate_shared_iip(self, si, port, vmi, vmi_obj):
        vn_obj = self._vnc_lib.virtual_network_read(id=vmi.virtual_network)
        iip_obj = self._allocate_iip_for_family(vn_obj, 'v4', si, port)
        self._vnc_lib.ref_update('instance-ip', iip_obj.uuid,
            'virtual-machine-interface', vmi_obj.uuid, None, 'ADD')
        return

    def set_port_service_health_check(self, port, vmi_obj):
        if port['service-health-check']:
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_obj.uuid,
                'service-health-check', port['service-health-check'], None, 'ADD')

    def set_port_static_routes(self, port, vmi_obj):
        if port['interface-route-table']:
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_obj.uuid,
                'interface-route-table', port['interface-route-table'], None, 'ADD')

    def set_port_allowed_address_pairs(self, port, vmi_obj):
        if port['allowed-address-pairs']:
            vmi_obj.set_virtual_machine_interface_allowed_address_pairs(
                port['allowed-address-pairs'])
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)

    def set_port_service_chain_ip(self, si, port, vmi, vmi_obj):
        if port['shared-ip']:
            self._allocate_shared_iip(si, port, vmi, vmi_obj)
            return

        iip = InstanceIpSM.get(vmi.instance_ip)
        if iip and not iip.service_instance_ip:
            iip_obj = self._vnc_lib.instance_ip_read(id=iip.uuid)
            iip_obj.set_service_instance_ip(True)
            self._vnc_lib.instance_ip_update(iip_obj)

    def get_port_config(self, st, si):
        st_if_list = st.params.get('interface_type', [])
        si_if_list = si.params.get('interface_list', [])

        port_config = {}
        for index in range(0, len(st_if_list)):
            try:
                si_if = si_if_list[index]
                st_if = st_if_list[index]
            except IndexError:
                continue

            port = {}
            port['type'] = st_if.get('service_interface_type')
            port['shared-ip'] = st_if.get('shared_ip')
            port['static-route-enable'] = st_if.get('static_route_enable')
            port['allowed-address-pairs'] = si_if.get('allowed_address_pairs')
            port['interface-route-table'] = None
            for irt_id in si.interface_route_tables:
                irt = InterfaceRouteTableSM.get(irt_id)
                if irt.service_interface_tag == port['type']:
                   port['interface-route-table'] = irt_id
            port['service-health-check'] = None
            for health_id in si.service_health_checks:
                health = ServiceHealthCheckSM.get(health_id)
                if health.service_interface_tag == port['type']:
                   port['service-health-check'] = health_id

            port_config[st_if.get('service_interface_type')] = port

        return port_config

    def update_port_tuple(self, pt_id):
        pt = PortTupleSM.get(pt_id)
        if not pt:
            return
        si = ServiceInstanceSM.get(pt.parent_key)
        if not si:
            return
        st = ServiceTemplateSM.get(si.service_template)
        port_config = self.get_port_config(st, si)
        if not port_config:
            return

        for vmi_id in pt.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            if not vmi:
                continue
            port = port_config[vmi.params.get('service_interface_type')]
            if not port:
                #log TODO
                continue

            vmi_obj = VirtualMachineInterface(fq_name=vmi.fq_name,
                name=vmi.name, parent_type='project')
            vmi_obj.uuid = vmi.uuid

            self.set_port_service_chain_ip(si, port, vmi, vmi_obj)
            self.set_port_allowed_address_pairs(port, vmi_obj)
            self.set_port_service_health_check(port, vmi_obj)
            self.set_port_static_routes(port, vmi_obj)

    def update_port_tuples(self):
        for si in ServiceInstanceSM.values():
            for pt_id in si.port_tuples:
                self.update_port_tuple(pt_id)
