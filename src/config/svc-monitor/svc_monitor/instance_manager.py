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
                 nova_client, agent_manager, args=None):
        self.logger = logger
        self._vnc_lib = vnc_lib
        self._args = args
        self._nc = nova_client
        self._agent_manager = agent_manager
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

    def mac_alloc(self, uuid):
        return '02:%s:%s:%s:%s:%s' % (uuid[0:2], uuid[2:4],
                                      uuid[4:6], uuid[6:8], uuid[9:11])

    def _get_default_security_group(self, vn):
        sg_fq_name = vn.fq_name[:-1] + ['default']
        for sg in SecurityGroupSM.values():
            if sg.fq_name == sg_fq_name:
                sg_obj = SecurityGroup()
                sg_obj.uuid = sg.uuid
                sg_obj.fq_name = sg.fq_name
                return sg_obj

        self.logger.error(
            "Security group not found %s" % (':'.join(sg_fq_name)))
        return None

    def _get_instance_name(self, si, inst_count):
        name = si.name + '__' + str(inst_count + 1)
        instance_name = "__".join(si.fq_name[:-1] + [name])
        return instance_name

    def _get_if_route_table_name(self, if_type, si):
        rt_name = si.uuid + ' ' + if_type
        rt_fq_name = si.fq_name[:-1] + [rt_name]
        return rt_fq_name

    def _get_project_obj(self, proj_fq_name):
        proj_obj = None
        for proj in ProjectSM.values():
            if proj.fq_name == proj_fq_name:
                proj_obj = Project()
                proj_obj.uuid = proj.uuid
                proj_obj.name = proj.name
                proj_obj.fq_name = proj.fq_name
                break
        if not proj_obj:
            self.logger.error("%s project not found" %
                                  (':'.join(proj_fq_name)))
        return proj_obj

    def _allocate_iip_for_family(self, vn_obj, iip_name, iip_family):
        if iip_family == 'v6':
            iip_name = iip_name + '-' + iip_family

        iip_obj = InstanceIp(name=iip_name, instance_ip_family=iip_family)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.set_service_instance_ip(True)
        for iip in InstanceIpSM.values():
            if iip.name == iip_name:
                iip_obj.uuid = iip.uuid
                return iip_obj

        if not iip_obj.uuid:
            try:
                self._vnc_lib.instance_ip_create(iip_obj)
            except RefsExistError:
                iip_obj = self._vnc_lib.instance_ip_read(fq_name=[iip_name])
            except BadRequest:
                return None

        InstanceIpSM.locate(iip_obj.uuid)
        return iip_obj

    def _allocate_iip(self, vn_obj, iip_name):
        iip_obj = self._allocate_iip_for_family(vn_obj, iip_name, 'v4')
        iipv6_obj = self._allocate_iip_for_family(vn_obj, iip_name, 'v6')
        return iip_obj, iipv6_obj

    def _link_and_update_iip_for_family(self, si, vmi_obj, iip_obj):
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

    def _link_and_update_iip(self, si, vmi_obj, iip_obj, iipv6_obj):
        if iip_obj:
            self._link_and_update_iip_for_family(si, vmi_obj, iip_obj)
        if iipv6_obj:
            self._link_and_update_iip_for_family(si, vmi_obj, iipv6_obj)

    def _link_fip_to_vmi(self, vmi_id, fip_id):
        fip = FloatingIpSM.get(fip_id)
        vmi = VirtualMachineInterfaceSM.get(vmi_id)
        if not fip or not vmi:
            self.logger.error("Failed associating fip %s to vmi %s" %
                              (fip_id, vmi_id))
            return
        if fip_id in vmi.floating_ips:
            return
        self._vnc_lib.ref_update('floating-ip', fip_id,
            'virtual-machine-interface', vmi_id, None, 'ADD')
        vmi.floating_ips.add(fip_id)

    def _link_sgs_to_vmi(self, vmi_id, sg_list):
        vmi = VirtualMachineInterfaceSM.get(vmi_id)
        if not vmi:
            return

        for sg_id in list(vmi.security_groups):
            if sg_id in sg_list:
                continue
            try:
                self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                    'security-group', sg_id, None, 'DELETE')
                vmi.security_groups.remove(sg_id)
            except Exception as e:
                self.logger.error(
                    "Security group detach from loadbalancer ports failed")

        for sg_id in sg_list:
            if sg_id in vmi.security_groups:
                continue
            try:
                self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                    'security-group', sg_id, None, 'ADD')
                vmi.security_groups.add(sg_id)
            except Exception as e:
                self.logger.error(
                    "Security group attach to loadbalancer ports failed")

    def _set_static_routes(self, nic, si):
        static_routes = nic['static-routes']
        if not static_routes:
            static_routes = {'route': []}

        proj_obj = self._get_project_obj(si.fq_name[:-1])
        if not proj_obj:
            return

        rt_fq_name = self._get_if_route_table_name(nic['type'], si)
        rt_obj = InterfaceRouteTable(name=rt_fq_name[-1],
                                     parent_obj=proj_obj, interface_route_table_routes=static_routes)
        for irt in InterfaceRouteTableSM.values():
            if irt.fq_name == rt_fq_name:
                rt_obj.set_interface_route_table_routes(static_routes)
                self._vnc_lib.interface_route_table_update(rt_obj)
                return rt_obj

        try:
            self._vnc_lib.interface_route_table_create(rt_obj)
        except RefsExistError:
            self._vnc_lib.interface_route_table_update(rt_obj)
        InterfaceRouteTableSM.locate(rt_obj.uuid)
        return rt_obj

    def update_static_routes(self, si):
        for nic in si.vn_info:
            if nic['static-route-enable']:
                self._set_static_routes(nic, si)

    def link_si_to_vm(self, si, st, instance_index, vm_uuid):
        vm_obj = VirtualMachine()
        vm_obj.uuid = vm_uuid
        instance_name = self._get_instance_name(si, instance_index)
        if st.virtualization_type == 'virtual-machine':
            vm_obj.fq_name = [vm_uuid]
        else:
            vm_obj.fq_name = [instance_name]
        vm_obj.set_display_name(instance_name + '__' + st.virtualization_type)
        si_obj = ServiceInstance()
        si_obj.uuid = si.uuid
        si_obj.fq_name = si.fq_name
        vm_obj.set_service_instance(si_obj)
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            self._vnc_lib.virtual_machine_update(vm_obj)

        VirtualMachineSM.locate(vm_obj.uuid)
        self.logger.info("Info: VM %s updated SI %s" %
                             (vm_obj.get_fq_name_str(), si_obj.get_fq_name_str()))

        # vm should be owned by tenant
        self._vnc_lib.chown(vm_uuid, si.parent_key)
        return vm_obj

    def create_port_tuple(self, si, st, instance_index, pt_uuid):
        if not st.virtualization_type == 'physical-device':
            return None
        si_obj = ServiceInstance()
        si_obj.uuid = si.uuid
        si_obj.fq_name = si.fq_name
        pt_obj = PortTuple(
            name = si.fq_name[-1]+'_'+str(instance_index),
            parent_obj=si_obj,
            uuid=pt_uuid)
        try:
            self._vnc_lib.port_tuple_create(pt_obj)
        except RefsExistError:
              self.logger.info("Info: PT %s of SI %s already exist" %
                             (pt_obj.get_fq_name_str(), si_obj.get_fq_name_str()))

        PortTupleSM.locate(pt_uuid,pt_obj.__dict__)
        self.logger.info("Info: PT %s updated SI %s" %
                             (pt_obj.get_fq_name_str(), si_obj.get_fq_name_str()))
        return pt_obj

    def create_service_vn(self, vn_name, vn_subnet, vn_subnet6,
                          proj_fq_name, user_visible=None):
        self.logger.info(
            "Creating network %s subnet %s subnet6 %s" %
            (vn_name, vn_subnet, vn_subnet6))

        proj_obj = self._get_project_obj(proj_fq_name)
        if not proj_obj:
            return

        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj)
        if user_visible is not None:
            id_perms = IdPermsType(enable=True, user_visible=user_visible)
            vn_obj.set_id_perms(id_perms)
        domain_name, project_name = proj_obj.get_fq_name()
        ipam_fq_name = [domain_name, project_name, 'default-network-ipam']
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        except NoIdError:
            ipam_obj = NetworkIpam()

        ipam_subnets = []
        cidr = vn_subnet.split('/')
        pfx = cidr[0]
        pfx_len = int(cidr[1])
        ipam_subnets.append(IpamSubnetType(subnet=SubnetType(pfx, pfx_len)))
        if vn_subnet6:
            cidr = vn_subnet6.split('/')
            pfx = cidr[0]
            pfx_len = int(cidr[1])
            ipam_subnets.append(IpamSubnetType(subnet=SubnetType(pfx, pfx_len)))
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(ipam_subnets))

        try:
            self._vnc_lib.virtual_network_create(vn_obj)
        except RefsExistError:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn_obj.get_fq_name())
        VirtualNetworkSM.locate(vn_obj.uuid)

        return vn_obj.uuid

    def _upgrade_config(self, st, si):
        left_vn = si.params.get('left_virtual_network', None)
        right_vn = si.params.get('right_virtual_network', None)
        mgmt_vn = si.params.get('management_virtual_network', None)

        st_if_list = st.params.get('interface_type', [])
        itf_list = []
        for index in range(0, len(st_if_list)):
            st_if_type = st_if_list[index]['service_interface_type']
            if st_if_type == svc_info.get_left_if_str():
                itf = ServiceInstanceInterfaceType(virtual_network=left_vn)
            elif st_if_type == svc_info.get_right_if_str():
                itf = ServiceInstanceInterfaceType(virtual_network=right_vn)
            elif st_if_type == svc_info.get_management_if_str():
                itf = ServiceInstanceInterfaceType(virtual_network=mgmt_vn)
            itf_list.append(itf)

        si_obj = ServiceInstance()
        si_obj.uuid = si.uuid
        si_obj.fq_name = si.fq_name
        si_props = ServiceInstanceType(**si.params)
        si_props.set_interface_list(itf_list)
        si_obj.set_service_instance_properties(si_props)
        self._vnc_lib.service_instance_update(si_obj)
        self.logger.notice("SI %s config upgraded for interfaces" %
                               (si_obj.get_fq_name_str()))

    def validate_network_config(self, st, si):
        if not si.params.get('interface_list'):
            self._upgrade_config(st, si)

        st_if_list = st.params.get('interface_type', [])
        si_if_list = si.params.get('interface_list', [])
        if not len(st_if_list) or not len(si_if_list):
            self.logger.notice("Interface list empty for ST %s SI %s" %
                                   ((':').join(st.fq_name), (':').join(si.fq_name)))
            return False

        si.vn_info = []
        config_complete = True
        for index in range(0, len(st_if_list)):
            vn_id = None
            try:
                si_if = si_if_list[index]
                st_if = st_if_list[index]
            except IndexError:
                continue

            nic = {}
            user_visible = True
            itf_type = st_if.get('service_interface_type')
            vn_fq_str = si_if.get('virtual_network', None)
            if not vn_fq_str:
                vn_id = self._check_create_service_vn(itf_type, si)
            else:
                try:
                    vn_id = self._vnc_lib.fq_name_to_id(
                        'virtual-network', vn_fq_str.split(':'))
                except NoIdError:
                    config_complete = False

            nic['type'] = itf_type
            nic['index'] = str(index + 1)
            nic['net-id'] = vn_id
            nic['shared-ip'] = st_if.get('shared_ip')
            nic['static-route-enable'] = st_if.get('static_route_enable')
            nic['static-routes'] = si_if.get('static_routes')
            nic['user-visible'] = user_visible
            si.vn_info.insert(index, nic)

        if config_complete:
            self.logger.info("SI %s info is complete" % si.fq_name)
            si.state = 'config_complete'
        else:
            self.logger.warning("SI %s info is not complete" % si.fq_name)
            si.state = 'config_pending'

        return config_complete

    def cleanup_svc_vm_ports(self, vmi_list):
        for vmi_id in vmi_list:
            try:
                vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                    id=vmi_id, fields=['instance_ip_back_refs', 'interface_route_table_refs'])
            except NoIdError:
                continue

            for iip in vmi_obj.get_instance_ip_back_refs() or []:
                try:
                    self._vnc_lib.ref_update('instance-ip', iip['uuid'],
                        'virtual-machine-interface', vmi_id, None, 'DELETE')
                except NoIdError:
                    pass

                iip_obj = self._vnc_lib.instance_ip_read(id=iip['uuid'])
                if not iip_obj.get_virtual_machine_interface_refs():
                    try:
                        self._vnc_lib.instance_ip_delete(id=iip['uuid'])
                        InstanceIpSM.delete(iip['uuid'])
                    except NoIdError:
                        pass

            for fip in vmi_obj.get_floating_ip_back_refs() or []:
                try:
                    self._vnc_lib.ref_update('floating-ip', fip['uuid'],
                        'virtual-machine-interface', vmi_id, None, 'DELETE')
                except NoIdError:
                    pass

            try:
                self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
                VirtualMachineInterfaceSM.delete(vmi_id)
            except (NoIdError, RefsExistError) as e:
                pass

            for irt in vmi_obj.get_interface_route_table_refs() or []:
                try:
                    self._vnc_lib.interface_route_table_delete(id=irt['uuid'])
                    InterfaceRouteTableSM.delete(irt['uuid'])
                except (NoIdError, RefsExistError) as e:
                    pass

    def _check_create_service_vn(self, itf_type, si):
        vn_id = None

        # search or create shared vn
        funcname = "get_" + itf_type + "_vn_name"
        func = getattr(svc_info, funcname)
        service_vn_name = func()
        funcname = "get_" + itf_type + "_vn_subnet"
        func = getattr(svc_info, funcname)
        service_vn_subnet = func()
        funcname = "get_" + itf_type + "_vn_subnet6"
        func = getattr(svc_info, funcname)
        service_vn_subnet6 = func()

        vn_fq_name = si.fq_name[:-1] + [service_vn_name]
        try:
            vn_id = self._vnc_lib.fq_name_to_id(
                'virtual-network', vn_fq_name)
        except NoIdError:
            vn_id = self.create_service_vn(service_vn_name,
                service_vn_subnet, service_vn_subnet6, si.fq_name[:-1])

        return vn_id

    def _check_create_netns_vm(self, instance_index, si, st, vm):
        # notify all the agents
        ret_val = self._agent_manager.pre_create_service_vm(
            instance_index, si, st, vm)
        if not ret_val:
            return None

        instance_name = self._get_instance_name(si, instance_index)
        vm_obj = VirtualMachine(instance_name)
        vm_obj.set_display_name(instance_name + '__' + st.virtualization_type)

        if not vm:
            si_obj = ServiceInstance()
            si_obj.uuid = si.uuid
            si_obj.fq_name = si.fq_name
            vm_obj.set_service_instance(si_obj)
            try:
                self._vnc_lib.virtual_machine_create(vm_obj)
            except RefsExistError:
                vm_obj = self._vnc_lib.virtual_machine_read(fq_name=vm_obj.fq_name)
                self._vnc_lib.ref_update('service-instance', si.uuid,
                    'virtual-machine', vm_obj.uuid)
            vm = VirtualMachineSM.locate(vm_obj.uuid)
            self.logger.info("Info: VM %s created for SI %s" %
                                 (instance_name, si_obj.get_fq_name_str()))
        else:
            vm_obj.uuid = vm.uuid

        for nic in si.vn_info:
            vmi_obj = self._create_svc_vm_port(nic, instance_name, si, st,
                                               local_preference=si.local_preference[
                                                   instance_index],
                                               vm_obj=vm_obj)

        # notify all the agents
        self._agent_manager.post_create_service_vm(instance_index, si, st, vm)

        return vm

    def _create_svc_vm_port(self, nic, instance_name, si, st,
                            local_preference=None, vm_obj=None, pi=None, pt=None):
        # get network
        vn = VirtualNetworkSM.get(nic['net-id'])
        if vn:
            vn_obj = VirtualNetwork()
            vn_obj.uuid = vn.uuid
            vn_obj.fq_name = vn.fq_name
        else:
            self.logger.error(
                "Virtual network %s not found for port create %s %s"
                % (nic['net-id'], instance_name, ':'.join(si.fq_name)))
            return

        # get project
        proj_obj = self._get_project_obj(si.fq_name[:-1])
        if not proj_obj:
            return

        vmi_create = False
        vmi_updated = False
        if_properties = None

        port_name = ('__').join([instance_name, nic['type'], nic['index']])
        port_fq_name = proj_obj.fq_name + [port_name]
        vmi_obj = VirtualMachineInterface(parent_obj=proj_obj, name=port_name)
        for vmi in VirtualMachineInterfaceSM.values():
            if vmi.fq_name == port_fq_name:
                vmi_obj.uuid = vmi.uuid
                vmi_obj.fq_name = vmi.fq_name
                if_properties = VirtualMachineInterfacePropertiesType(
                    **vmi.params)
                vmi_network = vmi.virtual_network
                if len(vmi.interface_route_tables):
                    vmi_irt = list(vmi.interface_route_tables)[0]
                else:
                    vmi_irt = None
                vmi_sg = vmi.security_groups
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
            self.logger.info("Info: VMI %s updated with VM %s" %
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
                if sg_obj:
                    vmi_obj.set_security_group(sg_obj)
                    vmi_updated = True

        if nic['static-route-enable']:
            if not vmi_irt:
                rt_obj = self._set_static_routes(nic, si)
                vmi_obj.set_interface_route_table(rt_obj)
                vmi_updated = True

        if vmi_create:
            if pi:
                pi_vnc = PhysicalInterface()
                pi_vnc.uuid = pi.uuid
                pi_vnc.fq_name = pi.fq_name
                vmi_obj.add_physical_interface(pi_vnc)
            if pt:
                pt_vnc = PortTuple()
                pt_vnc.uuid = pt.uuid
                pt_vnc.fq_name = pt.fq_name
                vmi_obj.add_port_tuple(pt_vnc)
            try:
                self._vnc_lib.virtual_machine_interface_create(vmi_obj)
            except RefsExistError:
                self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        elif vmi_updated:
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        # VMI should be owned by tenant
        self._vnc_lib.chown(vmi_obj.uuid, si.parent_key)

        VirtualMachineInterfaceSM.locate(vmi_obj.uuid)

        # instance ip
        iip_obj = None
        iipv6_obj = None
        if 'iip-id' in nic:
            iip = InstanceIpSM.get(nic['iip-id'])
            if iip:
                iip_obj = InstanceIp(name=iip.name)
                iip_obj.uuid = iip.uuid
        elif nic['shared-ip']:
            iip_name = "__".join(si.fq_name) + '-' + nic['type']
            iip_obj, iipv6_obj = self._allocate_iip(vn_obj, iip_name)
        else:
            iip_name = instance_name + '-' + nic['type'] + '-' + vmi_obj.uuid
            iip_obj, iipv6_obj = self._allocate_iip(vn_obj, iip_name)
        if not iip_obj and not iipv6_obj:
            self.logger.error(
                "Instance IP not allocated for %s %s"
                % (instance_name, proj_obj.name))
            return

        # instance-ip should be owned by tenant
        if iip_obj:
            self._vnc_lib.chown(iip_obj.uuid, si.parent_key)
        if iipv6_obj:
            self._vnc_lib.chown(iipv6_obj.uuid, si.parent_key)

        # set mac address
        if vmi_create:
            if iip_obj:
                mac_addr = self.mac_alloc(iip_obj.uuid)
            else:
                mac_addr = self.mac_alloc(iipv6_obj.uuid)
            mac_addrs_obj = MacAddressesType([mac_addr])
            vmi_obj.set_virtual_machine_interface_mac_addresses(mac_addrs_obj)
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        # link vmi to iip and set ha-mode
        self._link_and_update_iip(si, vmi_obj, iip_obj, iipv6_obj)

        # link vmi to fip
        if 'fip-id' in nic:
            self._link_fip_to_vmi(vmi_obj.uuid, nic['fip-id'])
        # link vmi to sg
        if 'sg-list' in nic:
            self._link_sgs_to_vmi(vmi_obj.uuid, nic['sg-list'])

        return vmi_obj

    def _associate_vrouter(self, si, vm):
        vrouter_name = None
        if not vm.virtual_router:
            chosen_vr = self.vrouter_scheduler.schedule(si, vm)
            if chosen_vr:
                vr = VirtualRouterSM.get(chosen_vr)
                vrouter_name = vr.name
                self.logger.notice("vrouter %s updated with vm %s" %
                                       (':'.join(vr.fq_name), vm.name))
                vm.update()
        else:
            vr = VirtualRouterSM.get(vm.virtual_router)
            vrouter_name = vr.name
        return vrouter_name

    def _update_local_preference(self, si, del_vm):
        if si.ha_mode != 'active-standby':
            return
        st = ServiceTemplateSM.get(si.service_template)
        if not st:
            return

        if si.local_preference[del_vm.index] == \
                svc_info.get_standby_preference():
            return

        si.local_preference[del_vm.index] = svc_info.get_standby_preference()
        other_index = si.max_instances - del_vm.index - 1
        si.local_preference[other_index] = svc_info.get_active_preference()

        for vm_id in si.virtual_machines:
            vm = VirtualMachineSM.get(vm_id)
            if vm:
                self._check_create_netns_vm(vm.index, si, st, vm)


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
            try:
                self._vnc_lib.ref_update('virtual-router', vm.virtual_router,
                    'virtual-machine', vm.uuid, None, 'DELETE')
                self.logger.info("vm %s deleted from vr %s" %
                    (vm.fq_name, vm.virtual_router))
            except NoIdError:
                pass

        try:
            self._vnc_lib.virtual_machine_delete(id=vm.uuid)
        except NoIdError:
            pass
        VirtualMachineSM.delete(vm.uuid)

    def check_service(self, si):
        service_up = True
        for vm_id in si.virtual_machines:
            vm = VirtualMachineSM.get(vm_id)
            if not vm:
                continue

            if not vm.virtual_router:
                self.logger.error("vrouter not found for vm %s" % vm.uuid)
                service_up = False
            else:
                vr = VirtualRouterSM.get(vm.virtual_router)
                if vr.agent_state:
                    continue
                self._vnc_lib.ref_update('virtual-router', vr.uuid,
                                         'virtual-machine', vm.uuid, None, 'DELETE')
                vr.update()
                self.logger.error(
                    "vrouter %s down for vm %s" % (vr.name, vm.uuid))
                service_up = False

        return service_up


class NetworkNamespaceManager(VRouterHostedManager):

    def create_service(self, st, si):
        if not self.validate_network_config(st, si):
            return

        # get current vm list
        vm_list = [None] * si.max_instances
        for vm_id in si.virtual_machines:
            vm = VirtualMachineSM.get(vm_id)
            vm_list[vm.index] = vm

        # if vn has changed then delete VMs
        if si.vn_changed:
            for vm in vm_list:
                if vm:
                    self.delete_service(vm)
            vm_list = [None] * si.max_instances
            si.vn_changed = False

        # create and launch vm
        si.state = 'launching'
        instances = []
        for index in range(0, si.max_instances):
            vm = self._check_create_netns_vm(index, si, st, vm_list[index])
            if not vm:
                continue

            vr_name = self._associate_vrouter(si, vm)
            if not vr_name:
                self.logger.error("No vrouter available for VM %s" %
                                      vm.name)

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

    def _get_loadbalancer_vmi(self, vmi_ids):
        for vmi_id in vmi_ids:
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            if not vmi or not (vmi.virtual_ip or vmi.loadbalancer):
                continue
            return vmi
        return None

    def add_fip_to_vip_vmi(self, fip):
        vip_vmi = self._get_loadbalancer_vmi(fip.virtual_machine_interfaces)
        if not vip_vmi:
            return

        if not vip_vmi.instance_ips:
            self.logger.error("VMI %s missing instance_ip backrefs" %
                               vip_vmi.uuid)
            return

        for iip_id in vip_vmi.instance_ips:
            iip = InstanceIpSM.get(iip_id)
            if not iip:
                self.logger.error("Instance IP object missing for iip_id %s" \
                                   % iip_id)
                continue
            for vmi_id in iip.virtual_machine_interfaces:
                if vmi_id == vip_vmi.uuid:
                    continue
                self._link_fip_to_vmi(vmi_id, fip.uuid)
            return

    def add_sg_to_vip_vmi(self, sg):
        vip_vmi = self._get_loadbalancer_vmi(sg.virtual_machine_interfaces)
        if not vip_vmi:
            return

        for iip_id in vip_vmi.instance_ips:
            iip = InstanceIpSM.get(iip_id)
            if not iip:
                continue
            for vmi_id in iip.virtual_machine_interfaces:
                if vmi_id == vip_vmi.uuid:
                    continue
                self._link_sgs_to_vmi(vmi_id, vip_vmi.security_groups)
            return
