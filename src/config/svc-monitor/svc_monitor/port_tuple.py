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

import abc
import six
import uuid

from cfgm_common import analytics_client
from cfgm_common import svc_info
from vnc_api.vnc_api import *
from config_db import *


@six.add_metaclass(abc.ABCMeta)
class PortTuple(object):

    def __init__(self, vnc_lib, db, logger, vrouter_scheduler,
                 nova_client, agent_manager, args=None):
        self.logger = logger
        self._vnc_lib = vnc_lib
        self._args = args
        self._nc = nova_client
        self._agent_manager = agent_manager
        self.vrouter_scheduler = vrouter_scheduler

    def set_port_service_health_check(self, st, si, nic, vmi_obj):
        vmi_obj.set_service_health_check(nic['service-health_check'])
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

    def set_port_allowed_address_pairs(self, st, si, nic, vmi_obj):
        vmi_obj.set_allowed_address_pairs(nic['allowed-address-pairs'])
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

    def set_port_static_routes(self, st, si, nic, vmi_obj):
        if nic['static-route-enable']:
            rt_obj = self._set_static_routes(nic, si)
            vmi_obj.set_interface_route_table(rt_obj)
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)

    def set_port_service_chain_ip(self, st, si, nic, vmi_obj):
        if nic['shared-ip']:
            vn_obj = self._vnc_lib.virtual_network_read(id=nic['net-id'])
            iip_name = si.uuid + '-' + nic['type']
            iip_obj, iipv6_obj = self._allocate_iip(vn_obj, iip_name)
            iip_obj.set_service_instance_ip(True)
            self._vnc_lib.instance_ip_update(iip_obj)
            iipv6_obj.set_service_instance_ip(True)
            self._vnc_lib.instance_ip_update(iipv6_obj)
        else:
            for iip_id in vmi.instance_ips:
                iip = InstanceIpSM.get(iip_id)
                if not iip:
                    continue
                if not iip.params['instance_ip_secondary']:
                    iip_obj = self._vnc_lib.instance_ip_read(id=iip_id)
                    iip_obj.set_service_instance_ip(True)
                    self._vnc_lib.instance_ip_update(iip_obj)

    def validate_network_config(self, st, si):
        st_if_list = st.params.get('interface_type', [])
        si_if_list = si.params.get('interface_list', [])

        port_config = {}
        config_complete = True
        for index in range(0, len(st_if_list)):
            try:
                si_if = si_if_list[index]
                st_if = st_if_list[index]
            except IndexError:
                continue

            nic = {}
            itf_type = st_if.get('service_interface_type')
            vn_fq_str = si_if.get('virtual_network', None)
            if not vn_fq_str:
                config_complete = False
            else:
                try:
                    vn_id = self._vnc_lib.fq_name_to_id(
                        'virtual-network', vn_fq_str.split(':'))
                except NoIdError:
                    self.logger.log_notice("virtual-network %s not found" % vn_fq_str)
                    config_complete = False

            nic['type'] = itf_type
            nic['net-id'] = vn_id
            nic['shared-ip'] = st_if.get('shared_ip')
            nic['static-route-enable'] = st_if.get('static_route_enable')
            nic['static-routes'] = si_if.get('static_routes')
            nic['allowed-address-pairs'] = si_if.get('allowed_address_pairs')
            nic['service-heath-check'] = si_if.get('service_health_check')
            port_config[itf_type] = nic

        if config_complete:
            self.logger.log_notice("si %s info is complete" % si.fq_name)
            return port_config
        else:
            self.logger.log_notice("si %s info is not complete" % si.fq_name)
            return None

    def create_service(self, st, si):
        pt = PortTupleSM(si.port_tuple)
        if not pt:
            return

        port_config = self.validate_network_config(st, si)
        if not port_config:
            return

        for vmi_id in pt.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            if not vmi:
                continue

            nic = port_config[vmi.params('service_interface_type')]
            vmi_obj = VirtualMachineInterface(
                parent_obj=proj_obj, name=vmi.name)
            vmi_obj.uuid = vmi.uuid
            vmi_obj.fq_name = vmi.fq_name

            self.set_port_service_chain_ip(st, si, nic, vmi_obj)
            self.set_port_allowed_address_pairs(st, si, nic, vmi_obj)
            self.set_port_service_health_check(st, si, vmi, nic, vmi_obj)

            try:
                self._vnc_lib.virtual_machine_update(vmi_obj)
            except NoIdError:
                pass
