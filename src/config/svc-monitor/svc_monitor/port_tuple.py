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
        self.logger = logger

    def handle_service_type(self):
        return 'port-tuple'

    def _allocate_iip_for_family(self, iip_family, si, port, vmi):
        create_iip = True
        update_vmi = False
        iip_name = si.uuid + '-' + port['type'] + '-' + iip_family
        for iip_id in si.instance_ips:
            iip = InstanceIpSM.get(iip_id)
            if iip and iip.name == iip_name:
                create_iip = False
                iip_id = iip.uuid
                if iip.uuid not in vmi.instance_ips:
                    update_vmi = True
                break

        if create_iip:
            iip_obj = InstanceIp(name=iip_name, instance_ip_family=iip_family)
            vn_obj = self._vnc_lib.virtual_network_read(id=vmi.virtual_network)
            iip_obj.add_virtual_network(vn_obj)
            iip_obj.set_service_instance_ip(True)
            iip_obj.set_instance_ip_secondary(True)
            iip_obj.set_instance_ip_mode('active-active')
            try:
                self._vnc_lib.instance_ip_create(iip_obj)
                self._vnc_lib.ref_relax_for_delete(iip_obj.uuid, vn_obj.uuid)
            except RefsExistError:
                self._vnc_lib.instance_ip_update(iip_obj)
            except Exception as e:
                return

            iip_id = iip_obj.uuid
            tag = ServiceInterfaceTag(interface_type=port['type'])
            self._vnc_lib.ref_update('service-instance', si.uuid,
                'instance-ip', iip_id, None, 'ADD', tag)
            InstanceIpSM.locate(iip_id)
            si.update()

        if create_iip or update_vmi:
            self._vnc_lib.ref_update('instance-ip', iip_id,
                'virtual-machine-interface', vmi.uuid, None, 'ADD')
            self._vnc_lib.ref_relax_for_delete(iip_id, vmi.uuid)
            vmi.update()

    def _allocate_shared_iip(self, si, port, vmi):
        self._allocate_iip_for_family('v4', si, port, vmi)
        self._allocate_iip_for_family('v6', si, port, vmi)

    def _allocate_health_check_iip_for_family(self, iip_family, port, vmi):
        iip_name = vmi.uuid + '-' + port['type'] + '-' + iip_family + '-health-check'
        iip_obj = InstanceIp(name=iip_name, instance_ip_family=iip_family)
        vn_obj = self._vnc_lib.virtual_network_read(id=vmi.virtual_network)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.set_service_health_check_ip(True)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        except Exception as e:
            return
        InstanceIpSM.locate(iip_obj.uuid)
        self._vnc_lib.ref_update('instance-ip', iip_obj.uuid,
            'virtual-machine-interface', vmi.uuid, None, 'ADD')
        vmi.update()

    def _allocate_health_check_iip(self, port, vmi):
        self._allocate_health_check_iip_for_family('v4', port, vmi)
        self._allocate_health_check_iip_for_family('v6', port, vmi)

    def _delete_health_check_iip(self, iip, vmi):
        for vmi_id in iip.virtual_machine_interfaces:
            self._vnc_lib.ref_update('instance-ip', iip.uuid,
                'virtual-machine-interface', vmi.uuid, None, 'DELETE')

        try:
            self._vnc_lib.instance_ip_delete(id=iip.uuid)
            InstanceIpSM.delete(iip.uuid)
        except NoIdError:
            return

    def update_health_check_iip(self, si, port, vmi):
        allocate_hc_iip = False
        for health_id, if_type in si.service_health_checks.items():
            health = ServiceHealthCheckSM.get(health_id)
            if not health:
                continue
            if if_type['interface_type'] != vmi.if_type:
                continue
            if health.params.get('health_check_type', None) != 'end-to-end':
                continue
            allocate_hc_iip = True
            break

        hc_iip = None
        for iip_id in list(vmi.instance_ips):
            iip = InstanceIpSM.get(iip_id)
            if not iip or not iip.service_health_check_ip:
                continue
            hc_iip = iip
            break

        if allocate_hc_iip:
            if not hc_iip:
                self._allocate_health_check_iip(port, vmi)
        elif hc_iip:
            self._delete_health_check_iip(hc_iip, vmi)

    def set_port_service_health_check(self, si, port, vmi):
        # handle add
        for health_id in port['service-health-checks']:
            if health_id in vmi.service_health_checks:
                continue
            self._vnc_lib.ref_update('virtual-machine-interface', vmi.uuid,
                'service-health-check', health_id, None, 'ADD')
            vmi.update()
        # handle deletes
        for health_id in list(vmi.service_health_checks):
            if health_id in port['service-health-checks']:
                continue
            self._vnc_lib.ref_update('virtual-machine-interface', vmi.uuid,
                'service-health-check', health_id, None, 'DELETE')
            vmi.update()

        # update health check ip
        self.update_health_check_iip(si, port, vmi)

    def set_port_static_routes(self, port, vmi):
        # handle add
        for irt_id in port['interface-route-tables']:
            if irt_id in vmi.interface_route_tables:
                continue
            self._vnc_lib.ref_update('virtual-machine-interface', vmi.uuid,
                'interface-route-table', irt_id, None, 'ADD')
            vmi.update()
        # handle deletes
        for irt_id in list(vmi.interface_route_tables):
            if irt_id in port['interface-route-tables']:
                continue
            self._vnc_lib.ref_update('virtual-machine-interface', vmi.uuid,
                'interface-route-table', irt_id, None, 'DELETE')
            vmi.update()

    def update_secondary_iip(self, vmi):
        for iip_id in list(vmi.instance_ips):
            iip = InstanceIpSM.get(iip_id)
            if not iip:
                continue
            if not iip.instance_ip_secondary or not iip.service_instance_ip:
                continue

            update = False
            if vmi.aaps and len(vmi.aaps):
                if iip.secondary_tracking_ip != vmi.aaps[0]['ip']:
                    tracking_ip = vmi.aaps[0]['ip']
                    ip_mode = vmi.aaps[0].get('address_mode', 'active-standby')
                    update = True
            else:
                if iip.secondary_tracking_ip:
                    tracking_ip = None
                    ip_mode = 'active-active'
                    update = True

            if not update:
                continue

            try:
                iip_obj = self._vnc_lib.instance_ip_read(id=iip.uuid)
                iip_obj.set_secondary_ip_tracking_ip(tracking_ip)
                iip_obj.set_instance_ip_mode(ip_mode)
                self._vnc_lib.instance_ip_update(iip_obj)
                iip.update()
            except NoIdError:
                self.logger.error("Instance IP %s update failed" % (iip.name))
                continue

    def set_port_allowed_address_pairs(self, port, vmi, vmi_obj):
        if not port['allowed-address-pairs'] or \
                not port['allowed-address-pairs'].get('allowed_address_pair', None):
            if vmi.aaps and len(vmi.aaps):
                vmi_obj.set_virtual_machine_interface_allowed_address_pairs(AllowedAddressPairs())
                self._vnc_lib.virtual_machine_interface_update(vmi_obj)
                vmi.update()
                self.update_secondary_iip(vmi)
            return

        aaps = port['allowed-address-pairs'].get('allowed_address_pair', None)
        update_aap = False
        if len(aaps) != len(vmi.aaps or []):
            update_aap = True
        else:
            for idx in range(0, len(vmi.aaps)):
                if vmi.aaps[idx]['ip'] != aaps[idx]['ip']:
                    update_aap = True
                    break
        if update_aap:
            vmi_obj.set_virtual_machine_interface_allowed_address_pairs(
                port['allowed-address-pairs'])
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)
            vmi.update()
            self.update_secondary_iip(vmi)

    def delete_shared_iip(self, iip):
        if not iip.service_instance_ip or not iip.instance_ip_secondary:
            return
        if iip.service_instance:
            return
        for vmi_id in list(iip.virtual_machine_interfaces):
            self._vnc_lib.ref_update('instance-ip', iip.uuid,
                'virtual-machine-interface', vmi_id, None, 'DELETE')

        try:
            self._vnc_lib.instance_ip_delete(id=iip.uuid)
            InstanceIpSM.delete(iip.uuid)
        except NoIdError:
            self.logger.error("Instance IP %s delete failed" % (iip.name))
            return

    def delete_old_vmi_links(self, vmi):
        if vmi.port_tuple:
            return
        for iip_id in list(vmi.instance_ips):
            iip = InstanceIpSM.get(iip_id)
            if not iip or not iip.service_instance:
                continue
            self._vnc_lib.ref_update('instance-ip', iip_id,
                'virtual-machine-interface', vmi.uuid, None, 'DELETE')
            vmi.instance_ips.remove(iip_id)

        for irt_id in list(vmi.interface_route_tables):
            irt = InterfaceRouteTableSM.get(irt_id)
            if irt and irt.service_instance:
                self._vnc_lib.ref_update('virtual-machine-interface', vmi.uuid,
                    'interface-route-table', irt.uuid, None, 'DELETE')
                vmi.interface_route_tables.remove(irt_id)

        for health_id in list(vmi.service_health_checks):
            health = ServiceHealthCheckSM.get(health_id)
            if health and health.service_instance:
                self._vnc_lib.ref_update('virtual-machine-interface', vmi.uuid,
                    'service-health-check', health.uuid, None, 'DELETE')
                vmi.service_health_checks.remove(health_id)

    def set_port_service_chain_ip(self, si, port, vmi):
        self._allocate_shared_iip(si, port, vmi)

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
            port['interface-route-tables'] = []
            for irt_id, if_type in si.interface_route_tables.items():
                irt = InterfaceRouteTableSM.get(irt_id)
                if irt and if_type['interface_type'] == port['type']:
                    port['interface-route-tables'].append(irt.uuid)
            port['service-health-checks'] = []
            for health_id, if_type in si.service_health_checks.items():
                health = ServiceHealthCheckSM.get(health_id)
                if health and if_type['interface_type'] == port['type']:
                    port['service-health-checks'].append(health.uuid)
            port_config[st_if.get('service_interface_type')] = port

        return port_config

    def update_port_tuple(self, vmi=None, pt_id=None):
        if vmi:
            self.delete_old_vmi_links(vmi)
            pt = PortTupleSM.get(vmi.port_tuple)
        if pt_id:
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

        for vmi_id in list(pt.virtual_machine_interfaces):
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            if not vmi:
                continue
            if not vmi.params:
                continue
            port = port_config[vmi.params.get('service_interface_type')]
            if not port:
                continue

            vmi_obj = VirtualMachineInterface(fq_name=vmi.fq_name,
                name=vmi.name, parent_type='project')
            vmi_obj.uuid = vmi.uuid

            self.set_port_service_chain_ip(si, port, vmi)
            self.set_port_allowed_address_pairs(port, vmi, vmi_obj)
            self.set_port_service_health_check(si, port, vmi)
            self.set_port_static_routes(port, vmi)

    def update_port_tuples(self):
        for si in ServiceInstanceSM.values():
            for pt_id in si.port_tuples:
                self.update_port_tuple(pt_id=pt_id)
        for iip in InstanceIpSM.values():
            self.delete_shared_iip(iip)
        for vmi in VirtualMachineInterfaceSM.values():
            self.delete_old_vmi_links(vmi)
