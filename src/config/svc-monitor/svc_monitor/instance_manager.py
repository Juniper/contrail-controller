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
class InstanceManager(object):

    def __init__(self, vnc_lib, db, logger, vrouter_scheduler,
                 nova_client, args=None):
        self.logger = logger
        self._vnc_lib = vnc_lib
        self._args = args
        self._nc = nova_client
        self.vrouter_scheduler = vrouter_scheduler

    @abc.abstractmethod
    def create_service(self, st, si):
        pass

    @abc.abstractmethod
    def delete_service(self, vm):
        pass

    @abc.abstractmethod
    def check_service(self, si):
        pass

    def _get_default_security_group(self, vn):
        sg_fq_name = vn.fq_name()[:-1] + ['default']
        for sg in SecurityGroupSM.values():
            if sg.fq_name == sg_fq_name:
                sg_obj = SecurityGroup()
                sg_obj.uuid = sg.uuid
                sg_obj.fq_name = sg.fq_name
                return sg_obj

        self.logger.log(
            "Error: Security group not found %s" % (sg.fq_name.join(':')))
        return None

    def _get_instance_name(self, si, inst_count):
        name = si.name + '__' + str(inst_count + 1)
        instance_name = "__".join(si.fq_name[:-1] + [name])
        return instance_name

    def _get_if_route_table_name(self, if_type, si):
        rt_name = si.uuid + ' ' + if_type
        rt_fq_name = si.fq_name[:-1] + [rt_name]
        return rt_fq_name

    def _allocate_iip(self, vn_obj, iip_name):
        iip_obj = InstanceIp(name=iip_name)
        iip_obj.add_virtual_network(vn_obj)
        for iip in InstanceIpSM.values():
            if iip.name == iip_name:
                iip_obj.uuid = iip.uuid
                return iip_obj

        if not iip_obj.uuid:
            try:
                self._vnc_lib.instance_ip_create(iip_obj)
            except RefsExistError:
                pass

        return iip_obj

    def _set_static_routes(self, nic, si):
        static_routes = nic['static-routes']
        if not static_routes:
            static_routes = {'route':[]}

        proj_fq_name = si.fq_name[:-1]
        proj_obj = None
        for proj in ProjectSM.values():
            if proj.fq_name == proj_fq_name:
                proj_obj = Project()
                proj_obj.uuid = proj.uuid
                proj_obj.fq_name = proj.fq_name
                break
        if not proj_obj:
            self.logger.log("%s project not found" % (proj_fq_name.join(':')))
            return

        rt_fq_name = self._get_if_route_table_name(nic['type'], si)
        rt_obj = InterfaceRouteTable(name=rt_fq_name[-1],
            parent_obj=proj_obj, interface_route_table_routes=static_routes)
        for irt in InterfaceRouteTableSM.values():
            if irt.fq_name == rt_fq_name:
                rt_obj.set_interface_route_table_routes(static_routes)
                self._vnc_lib.interface_route_table_update(rt_obj)
                return

        self._vnc_lib.interface_route_table_create(rt_obj)
        return rt_obj

    def update_static_routes(self, si):
        for nic in si.vn_info:
            if nic['static-route-enable']:
                self._set_static_routes(nic, si)

    def link_si_to_vm(self, si, st, instance_index, vm_uuid):
        vm_obj = VirtualMachine()
        vm_obj.uuid = vm_uuid
        vm_obj.fq_name = [vm_uuid]
        instance_name = self._get_instance_name(si, instance_index)
        vm_obj.set_display_name(instance_name + '__' + st.virtualization_type)
        si_obj = ServiceInstance()
        si_obj.uuid = si.uuid
        si_obj.fq_name = si.fq_name
        vm_obj.set_service_instance(si_obj)
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            self._vnc_lib.virtual_machine_update(vm_obj)

        self.logger.log("Info: VM %s updated SI %s" %
            (vm_obj.get_fq_name_str(), si_obj.get_fq_name_str()))

    def create_service_vn(self, vn_name, vn_subnet,
                          proj_fq_name, user_visible=None):
        self.logger.log(
            "Creating network %s subnet %s" % (vn_name, vn_subnet))

        proj_obj = None
        for proj in ProjectSM.values():
            if proj.fq_name == proj_fq_name:
                proj_obj = Project()
                proj_obj.uuid = proj.uuid
                proj_obj.fq_name = proj.fq_name
                break
        if not proj_obj:
            self.logger.log("%s project not found" % (proj_fq_name.join(':')))
            return None

        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj)
        if user_visible is not None:
            id_perms = IdPermsType(enable=True, user_visible=user_visible)
            vn_obj.set_id_perms(id_perms)
        domain_name, project_name = proj_obj.get_fq_name()
        ipam_fq_name = [domain_name, 'default-project', 'default-network-ipam']
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

    def validate_network_config(self, st, si):
        config_complete = True
        st_if_list = st.params.get('interface_type')
        si_if_list = si.params.get('interface_list')
        si.vn_info = []
        for index in range(0, len(st_if_list)):
            vn_id = None
            try:
                si_if = si_if_list[index]
                st_if = st_if_list[index]
            except IndexError:
                continue

            user_visible = True
            itf_type = st_if.get('service_interface_type')
            vn_fq_str = si_if.get('virtual_network', None)
            if (itf_type == svc_info.get_left_if_str() and
                    (st.params.get('service_type') ==
                     svc_info.get_snat_service_type())):
                vn_id = self._create_snat_vn(si, vn_fq_str, index)
                user_visible = False
            elif (itf_type == svc_info.get_right_if_str() and
                    (st.params.get('service_type') ==
                     svc_info.get_lb_service_type())):
                iip_id, vn_id = self._get_vip_vmi_iip(si)
                nic['iip-id'] = iip_id
                user_visible = False
            elif not vn_fq_str or vn_fq_str == '':
                vn_id = self._check_create_service_vn(itf_type, si)
            else:
                try:
                    vn_id = self._vnc_lib.fq_name_to_id(
                        'virtual-network', vn_fq_str.split(':'))
                except NoIdError:
                    config_complete = False

            nic = {}
            nic['type'] = st_if.get('service_interface_type')
            nic['index'] = str(index + 1)
            nic['net-id'] = vn_id
            nic['shared-ip'] = st_if.get('shared_ip')
            nic['static-route-enable'] = st_if.get('static_route_enable')
            nic['static-routes'] = si_if.get('static_routes')
            nic['user-visible'] = user_visible
            si.vn_info.insert(index, nic)

        if config_complete:
            self.logger.log("SI %s info is complete" % si.fq_name)
            si.state = 'config_complete'
        else:
            self.logger.log("Warn: SI %s info is not complete" % si.fq_name)
            si.state = 'config_pending'

        return config_complete

    def cleanup_svc_vm_ports(self, vmi_list):
        for vmi_id in vmi_list:
            try:
                vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                    id=vmi_id)
            except NoIdError:
                continue

            for iip in vmi_obj.get_instance_ip_back_refs() or []:
                try:
                    iip_obj = self._vnc_lib.instance_ip_read(id=iip['uuid'])
                except NoIdError:
                    continue
                iip_obj.del_virtual_machine_interface(vmi_obj)
                vmi_refs = iip_obj.get_virtual_machine_interface_refs()
                if not vmi_refs:
                    self._vnc_lib.instance_ip_delete(id=iip['uuid'])
                else:
                    self._vnc_lib.instance_ip_update(iip_obj)
            self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)

    def _check_create_netns_vm(self, instance_index, si, st, vm):
        instance_name = self._get_instance_name(si, instance_index)
        vm_obj = VirtualMachine(instance_name)
        vm_obj.set_display_name(instance_name + '__' + st.virtualization_type)

        if not vm:
            si_obj = ServiceInstance()
            si_obj.uuid = si.uuid
            si_obj.fq_name = si.fq_name
            vm_obj.set_service_instance(si_obj)
            self._vnc_lib.virtual_machine_create(vm_obj)
            vm = VirtualMachineSM.locate(vm_obj.uuid)
            self.logger.log("Info: VM %s created for SI %s" %
                (instance_name, si_obj.get_fq_name_str()))
        else:
            vm_obj.uuid = vm.uuid

        for nic in si.vn_info:
            vmi_obj = self._create_svc_vm_port(nic, instance_name, si, st,
                local_preference=si.local_preference[instance_index],
                vm_obj=vm_obj)

        return vm

    def _create_svc_vm_port(self, nic, instance_name, si, st,
                            local_preference=None, vm_obj=None):
        # get network
        vn = VirtualNetworkSM.get(nic['net-id'])
        if vn:
            vn_obj = VirtualNetwork()
            vn_obj.uuid = vn.uuid
            vn_obj.fq_name = vn.fq_name
        else:
            self.logger.log(
                "Error: Virtual network %s not found for port create %s %s"
                % (nic['net-id'], instance_name, ':'.join(si.fq_name)))
            return

        # get project
        proj_obj = None
        proj_fq_name = si.fq_name[:-1]
        for proj in ProjectSM.values():
            if proj.fq_name == proj_fq_name:
                proj_obj = Project()
                proj_obj.uuid = proj.uuid
                proj_obj.fq_name = proj.fq_name
                break
        if not proj_obj:
            self.logger.log("%s project not found" % (proj_fq_name.join(':')))
            return

        vmi_create = False
        vmi_updated = False
        if_properties = None
        
        port_name = ('__').join([instance_name, nic['type'], nic['index']])
        port_fq_name = proj_fq_name + [port_name]
        vmi_obj = VirtualMachineInterface(parent_obj=proj_obj, name=port_name)
        for vmi in VirtualMachineInterfaceSM.values():
            if vmi.fq_name == port_fq_name:
                vmi_obj.uuid = vmi.uuid
                vmi_obj.fq_name = vmi.fq_name
                if_properties = VirtualMachineInterfacePropertiesType(**vmi.params)
                vmi_network = vmi.virtual_network
                vmi_irt = vmi.interface_route_table
                vmi_sg = vmi.security_group
                vmi_vm = vmi.virtual_machine
                break
        if not vmi_obj.uuid:
            if nic['user-visible'] is not None:
                id_perms = IdPermsType(enable=True,
                    user_visible=nic['user-visible'])
                vmi_obj.set_id_perms(id_perms)
            vmi_create = True
            vmi_network = None
            vmi_irt = None
            vmi_sg = None
            vmi_vm = None

        # set vm, vn, itf_type, sg and static routes
        if not vmi_vm and vm_obj:
            vmi_obj.set_virtual_machine(vm_obj)
            vmi_updated = True
            self.logger.log("Info: VMI %s updated with VM %s" %
                (vmi_obj.get_fq_name_str(), instance_name))

        if not vmi_network:
            vmi_obj.set_virtual_network(vn_obj)
            vmi_updated = True

        if if_properties is None:
            if_properties = VirtualMachineInterfacePropertiesType(nic['type'])
            vmi_obj.set_virtual_machine_interface_properties(if_properties)
            vmi_updated = True

        if local_preference:
            if local_preference != if_properties.get_local_preference():
                if_properties.set_local_preference(local_preference)
                vmi_obj.set_virtual_machine_interface_properties(if_properties)
                vmi_updated = True

        if (st.params.get('service_mode') in ['in-network', 'in-network-nat'] and
            proj_obj.name != 'default-project'):
            if not vmi_sg:
                sg_obj = self._get_default_security_group(vn_obj)
                vmi_obj.set_security_group(sg_obj)
                vmi_updated = True

        if nic['static-route-enable']:
            if not vmi_irt:
                rt_obj = self._set_static_routes(nic, si)
                vmi_obj.set_interface_route_table(rt_obj)
                vmi_updated = True

        if vmi_create:
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        elif vmi_updated:
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        # instance ip
        if 'iip-id' in nic:
            iip = InstanceIpSM.get(nic['iip-id'])
            iip_obj = None
            if iip:
                iip_obj = InstaneIp()
                iip_obj.uuid = iip.uuid
                iip_obj.name = iip.name
        elif nic['shared-ip']:
            iip_name = "__".join(si.fq_name) + '-' + nic['type']
            iip_obj = self._allocate_iip(vn_obj, iip_name)
        else:
            iip_name = instance_name + '-' + nic['type'] + '-' + vmi_obj.uuid
            iip_obj = self._allocate_iip(vn_obj, iip_name)
        if not iip_obj:
            self.logger.log(
                "Error: Instance IP not allocated for %s %s"
                % (instance_name, proj_obj.name))
            return

        # check if vmi already linked to iip
        iip_update = True
        iip = InstanceIpSM.get(iip_obj.uuid)
        if iip:
            for vmi_id in iip.virtual_machine_interfaces:
                vmi = VirtualMachineInterfaceSM.get(vmi_id)
                if vmi and vmi.uuid == vmi_obj.uuid:
                    iip_update = False
        else:
            vmi_refs = iip_obj.get_virtual_machine_interface_refs()
            for vmi_ref in vmi_refs or []:
                if vmi_obj.uuid == vmi_ref['uuid']:
                    iip_update = False

        if iip_update:
            if si.ha_mode:
                iip_obj.set_instance_ip_mode(si.ha_mode)
            elif si.max_instances > 1:
                iip_obj.set_instance_ip_mode(u'active-active')
            else:
                iip_obj.set_instance_ip_mode(u'active-standby')

            iip_obj.add_virtual_machine_interface(vmi_obj)
            self._vnc_lib.instance_ip_update(iip_obj)

        return vmi_obj

    def _associate_vrouter(self, si, vm):
        vrouter_name = None
        if not vm.virtual_router:
            chosen_vr_fq_name = None
            chosen_vr_fq_name = self.vrouter_scheduler.schedule(
                si.uuid, vm.uuid)
            if chosen_vr_fq_name:
                vrouter_name = chosen_vr_fq_name[-1]
                self.logger.log("Info: VRouter %s updated with VM %s" %
                    (':'.join(chosen_vr_fq_name), vm.name))
        else:
            vr = VirtualRouterSM.get(vm.virtual_router)
            vrouter_name = vr.name
        return vrouter_name

class VRouterHostedManager(InstanceManager):
    @abc.abstractmethod
    def create_service(self, st, si):
        pass

    def delete_service(self, vm):
        vmi_list = []
        for vmi_id in vm.virtual_machine_interfaces:
            vmi_list.append(vmi_id)
        self.cleanup_svc_vm_ports(vmi_list)

        if vm.virtual_router:
            vm_obj = VirtualMachine()
            vm_obj.uuid = vm.uuid
            vm_obj.fq_name = vm.fq_name
            vr_obj = self._vnc_lib.virtual_router_read(id=vm.virtual_router)
            vr_obj.del_virtual_machine(vm_obj)
            self._vnc_lib.virtual_router_update(vr_obj)
            self.logger.log("UPDATE: vm %s deleted from vrvm %s" %
                (vm_obj.get_fq_name_str(), vr_obj.get_fq_name_str()))

        self._vnc_lib.virtual_machine_delete(id=vm.uuid)

    def check_service(self, si):
        service_up = True
        for vm_id in si.virtual_machines:
            vm = VirtualMachineSM.get(vm_id)
            if not vm.virtual_router:
                self.logger.log("ERROR: vrouter not found for vm %s" % vm.uuid)
                service_up = False
            else:
                vr = VirtualRouterSM.get(vm.virtual_router)
                if self.vrouter_scheduler.vrouter_running(vr.name):
                    continue
                vr_obj = VirtualRouter()
                vr_obj.uuid = vr.vr_id
                vr_obj.fq_name = vr.fq_name
                vm_obj = VirtualMachine()
                vm_obj.uuid = vm.uuid
                vm_obj.fq_name = vm.fq_name
                vr_obj.del_virtual_machine(vm_obj)
                self._vnc_lib.virtual_router_update(vr_obj)
                self.logger.log("ERROR: vrouter down for vm %s" % vm.uuid)
                service_up = False

        return service_up


class NetworkNamespaceManager(VRouterHostedManager):

    def create_service(self, st, si):
        if not self.validate_network_config(st, si):
            return

        # get current vm list
        vm_list = [None for i in range(0, si.max_instances)]
        for vm_id in si.virtual_machines:
            vm = VirtualMachineSM.get(vm_id)
            index = int(vm.display_name.split('__')[-2]) - 1
            vm_list[index] = vm

        # create and launch vm
        si.state = 'launching'
        instances = []
        for index in range(0, si.max_instances):
            vm = self._check_create_netns_vm(index, si, st, vm_list[index])
            if not vm:
                continue

            vr_name = self._associate_vrouter(si, vm)

            if si.local_preference[index] == svc_info.get_standby_preference():
                ha = ("standby: %s" % (si.local_preference[index]))
            else:
                ha = ("active: %s" % (si.local_preference[index]))
            instances.append({'uuid': vm.uuid, 'vr_name': vr_name, 'ha': ha})

        # uve trace
        si.state = 'active'
        self.logger.uve_svc_instance((':').join(si.fq_name),
            status='CREATE', vms=instances,
            st_name=(':').join(st.fq_name))

    def _create_snat_vn(self, si, vn_fq_str, index):
        vn_name = '%s_%s' % (svc_info.get_snat_left_vn_prefix(),
                             si.name)
        vn_fq_name = si.fq_name[:-1] + [vn_name]
        try:
            vn_id = self._vnc_lib.fq_name_to_id(
                'virtual-network', vn_fq_name)
        except NoIdError:
            snat_subnet = svc_info.get_snat_left_subnet()
            vn_id = self.create_service_vn(vn_name, snat_subnet,
                si.fq_name[:-1], user_visible=False)

        if vn_fq_str != ':'.join(vn_fq_name):
            si_obj = ServiceInstance()
            si_obj.uuid = si.uuid
            si_obj.fq_name = si.fq_name
            si_props = ServiceInstanceType(**si.params)
            left_if = ServiceInstanceInterfaceType(
                virtual_network=':'.join(vn_fq_name))
            si_props.insert_interface_list(index, left_if)
            si_obj.set_service_instance_properties(si_props)
            self._vnc_lib.service_instance_update(si_obj)
            self.logger.log("Info: SI %s updated with left vn %s" %
                             (si_obj.get_fq_name_str(), vn_fq_str))

        return vn_id

    def _get_vip_vmi_iip(self, si):
        if not si.loadbalancer_pool:
            return None, None

        pool = LoadbalancerPoolSM.get(si.loadbalancer_pool)
        if not pool.virtual_ip:
            return None, None

        vip = VirtualIpSM.get(pool.virtual_ip)
        if not vip.virtual_machine_interface:
            return None, None

        vmi = VirtualMachineInterfaceSM.get(vip.virtual_machine_interface)
        if not vmi.instance_ip or not vmi.virtual_network:
            return None, None

        return vmi.instance_ip, vmi.virtual_network
