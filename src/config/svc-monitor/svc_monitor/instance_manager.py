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

from cfgm_common import analytics_client
from cfgm_common import svc_info
from vnc_api.vnc_api import *

from novaclient import client as nc
from novaclient import exceptions as nc_exc


@six.add_metaclass(abc.ABCMeta)
class InstanceManager(object):

    def __init__(self, vnc_lib, db, logger, vrouter_scheduler, args=None):
        self.logger = logger
        self.db = db
        self._vnc_lib = vnc_lib
        self._args = args
        self._nova = {}
        self.vrouter_scheduler = vrouter_scheduler

    @abc.abstractmethod
    def create_service(self, st_obj, si_obj):
        pass

    @abc.abstractmethod
    def delete_service(self, vm_uuid, proj_name=None):
        pass

    def _get_default_security_group(self, vn_obj):
        sg_fq_name = vn_obj.get_fq_name()[:-1]
        sg_fq_name.append('default')
        sg_obj = None
        try:
            sg_obj = self._vnc_lib.security_group_read(fq_name=sg_fq_name)
        except Exception as e:
            self.logger.log(
                "Error: Security group default not found %s" % (proj_obj.name))
        return sg_obj
    #end _get_default_security_group

    def _allocate_iip(self, proj_obj, vn_obj, iip_name):
        try:
            iip_obj = self._vnc_lib.instance_ip_read(fq_name=[iip_name])
        except NoIdError:
            iip_obj = None

        # allocate ip
        if not iip_obj:
            try:
                addr = self._vnc_lib.virtual_network_ip_alloc(vn_obj)
            except Exception as e:
                return iip_obj

            iip_obj = InstanceIp(name=iip_name, instance_ip_address=addr[0])
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)

        return iip_obj
    #end _allocate_iip

    def _set_static_routes(self, nic, vmi_obj, proj_obj, si_obj):
        # set static routes
        static_routes = nic['static-routes']
        if not static_routes:
            static_routes = {'route':[]}

        try:
            domain_name, proj_name = proj_obj.get_fq_name()
            rt_name = si_obj.uuid + ' ' + nic['type']
            rt_fq_name = [domain_name, proj_name, rt_name]
            rt_obj = self._vnc_lib.interface_route_table_read(
                fq_name=rt_fq_name)
            rt_obj.set_interface_route_table_routes(static_routes)
        except NoIdError:
            proj_obj = self._vnc_lib.project_read(
                fq_name=si_obj.get_parent_fq_name())
            rt_obj = InterfaceRouteTable(
                name=rt_name,
                parent_obj=proj_obj,
                interface_route_table_routes=static_routes)
            self._vnc_lib.interface_route_table_create(rt_obj)

        return rt_obj
    #end _set_static_routes

    def _create_svc_vm_port(self, nic, vm_name, st_obj, si_obj):
        # get virtual network
        try:
            vn_obj = self._vnc_lib.virtual_network_read(id=nic['net-id'])
        except NoIdError:
            self.logger.log(
                "Error: Virtual network %s not found for port create %s %s"
                % (nic['net-id'], vm_name, proj_obj.name))
            return

        # create or find port
        port_name = vm_name + '-' + nic['type']
        proj_fq_name = si_obj.get_parent_fq_name()
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        port_fq_name = proj_fq_name + [port_name]
        vmi_created = False
        try:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(fq_name=port_fq_name)
        except NoIdError:
            vmi_obj = VirtualMachineInterface(parent_obj=proj_obj, name=port_name)
            vmi_created = True

        # set vn, itf_type, sg and static routes
        vmi_obj.set_virtual_network(vn_obj)
        if_properties = VirtualMachineInterfacePropertiesType(nic['type'])
        vmi_obj.set_virtual_machine_interface_properties(if_properties)
        st_props = st_obj.get_service_template_properties()
        if (st_props.service_mode in ['in-network', 'in-network-nat'] and
            proj_obj.name != 'default-project'):
            sg_obj = self._get_default_security_group(vn_obj)
            vmi_obj.set_security_group(sg_obj)
        if nic['static-route-enable']:
            rt_obj = self._set_static_routes(nic, vmi_obj, proj_obj, si_obj)
            vmi_obj.set_interface_route_table(rt_obj)

        if vmi_created:
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        else:
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        # set instance ip
        if nic['shared-ip']:
            iip_name = si_obj.name + '-' + nic['type']
        else:
            iip_name = vm_name + '-' + nic['type']
        iip_obj = self._allocate_iip(proj_obj, vn_obj, iip_name)
        if not iip_obj:
            self.logger.log(
                "Error: Instance IP not allocated for %s %s"
                % (vm_name, proj_obj.name))
            return

        # set active-standy flag for instance ip
        si_props = si_obj.get_service_instance_properties()
        if si_props.get_scale_out():
            max_instances = si_props.get_scale_out().get_max_instances()
        else:
            max_instances = 1
        if max_instances > 1:
            iip_obj.set_instance_ip_mode(u'active-active');
        else:
            iip_obj.set_instance_ip_mode(u'active-standby');
        iip_obj.add_virtual_machine_interface(vmi_obj)
        self._vnc_lib.instance_ip_update(iip_obj)

        return vmi_obj
    # end _create_svc_vm_port

    def _create_svc_vn(self, vn_name, vn_subnet, proj_obj):
        self.logger.log(
            "Creating network %s subnet %s" % (vn_name, vn_subnet))

        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj)
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

        return vn_obj.uuid
    # end _create_svc_vn

    def _get_vn_id(self, proj_obj, vn_fq_name_str, itf_type):
        vn_id = None

        if vn_fq_name_str:
            # search for provided vn
            vn_fq_name = vn_fq_name_str.split(':')
            try:
                vn_id = self._vnc_lib.fq_name_to_id(
                    'virtual-network', vn_fq_name)
            except NoIdError:
                self.logger.log("Error: vn_fq_name %s not found" %
                    (vn_fq_name_str))
        else:
            # search or create shared vn
            funcname = "get_" + itf_type + "_vn_name"
            func = getattr(svc_info, funcname)
            shared_vn_name = func()
            funcname = "get_" + itf_type + "_vn_subnet"
            func = getattr(svc_info, funcname)
            shared_vn_subnet = func()

            vn_fq_name = proj_obj.get_fq_name() + [shared_vn_name]
            try:
                vn_id = self._vnc_lib.fq_name_to_id(
                    'virtual-network', vn_fq_name)
            except NoIdError:
                vn_id = self._create_svc_vn(shared_vn_name,
                    shared_vn_subnet, proj_obj)

        return vn_id
    # end _get_vn_id

    def _get_virtualization_type(self, st_props):
        service_type = st_props.get_service_virtualization_type()
        return service_type or svc_info.get_vm_instance_type()
    # end _get_virtualization_type

    def _get_nic_info(self, si_obj, si_props, st_props):
        si_if_list = si_props.get_interface_list()
        st_if_list = st_props.get_interface_type()
        if si_if_list and (len(si_if_list) != len(st_if_list)):
            self.logger.log("Error: IF mismatch template %s instance %s" %
                             (len(st_if_list), len(si_if_list)))
            return

        # check and create virtual networks
        nics = []
        proj_fq_name = si_obj.get_parent_fq_name()
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        for idx in range(0, len(st_if_list)):
            nic = {}
            st_if = st_if_list[idx]
            itf_type = st_if.service_interface_type

            # set vn id
            if si_if_list and st_props.get_ordered_interfaces():
                si_if = si_if_list[idx]
                vn_fq_name_str = si_if.get_virtual_network()
                nic['static-routes'] = si_if.get_static_routes()
            else:
                funcname = "get_" + itf_type + "_virtual_network"
                func = getattr(si_props, funcname)
                vn_fq_name_str = func()

            if (itf_type == svc_info.get_left_if_str() and
                    (st_props.get_service_type() ==
                     svc_info.get_snat_service_type())):
                vn_id = self._create_snat_vn(proj_obj, si_obj,
                                             si_props, vn_fq_name_str)
            else:
                vn_id = self._get_vn_id(proj_obj, vn_fq_name_str, itf_type)
            if vn_id is None:
                continue

            nic['net-id'] = vn_id
            nic['type'] = itf_type
            nic['shared-ip'] = st_if.shared_ip
            nic['static-route-enable'] = st_if.get_static_route_enable()
            nics.append(nic)

        return nics


class VirtualMachineManager(InstanceManager):

    def _create_svc_vm(self, vm_name, image_name, nics,
                       flavor_name, st_obj, si_obj, avail_zone):

        proj_name = si_obj.get_parent_fq_name()[-1]
        if flavor_name:
            flavor = self.novaclient_oper('flavors', 'find', proj_name,
                                           name=flavor_name)
        else:
            flavor = self.novaclient_oper('flavors', 'find', proj_name,
                                           ram=4096)

        image = ''
        try:
            image = self.novaclient_oper('images', 'find', proj_name,
                                          name=image_name)
        except nc_exc.NotFound:
            self.logger.log(
                "Error: Image %s not found in project %s"
                % (image_name, proj_name))
            return
        except nc_exc.NoUniqueMatch:
            self.logger.log(
                "Error: Multiple images %s found in project %s"
                % (image_name, proj_name))
            return

        # create port
        nics_with_port = []
        for nic in nics:
            nic_with_port = {}
            vmi_obj = self._create_svc_vm_port(nic, vm_name, st_obj, si_obj)
            nic_with_port['port-id'] = vmi_obj.get_uuid()
            nics_with_port.append(nic_with_port)

        # launch vm
        self.logger.log('Launching VM : ' + vm_name)
        nova_vm = self.novaclient_oper('servers', 'create', proj_name,
                                        name=vm_name, image=image,
                                        flavor=flavor, nics=nics_with_port,
                                        availability_zone=avail_zone)
        nova_vm.get()
        self.logger.log('Created VM : ' + str(nova_vm))
        return nova_vm
    # end _create_svc_vm

    def create_service(self, st_obj, si_obj):
        si_props = si_obj.get_service_instance_properties()
        st_props = st_obj.get_service_template_properties()
        if st_props is None:
            self.logger.log("Cannot find service template associated to "
                             "service instance %s" % si_obj.get_fq_name_str())
            return

        flavor = st_props.get_flavor()
        image_name = st_props.get_image_name()
        if image_name is None:
            self.logger.log("Error: Image name not present in %s" %
                             (st_obj.name))
            return

        # populate nic information
        nics = self._get_nic_info(si_obj, si_props, st_props)

        # get availability zone
        avail_zone = None
        if st_props.get_availability_zone_enable():
            avail_zone = si_props.get_availability_zone()
        elif self._args.availability_zone:
            avail_zone = self._args.availability_zone

        # create and launch vm
        vm_back_refs = si_obj.get_virtual_machine_back_refs()
        proj_name = si_obj.get_parent_fq_name()[-1]
        max_instances = si_props.get_scale_out().get_max_instances()
        for inst_count in range(0, max_instances):
            instance_name = si_obj.name + '_' + str(inst_count + 1)
            exists = False
            for vm_back_ref in vm_back_refs or []:
                vm = self.novaclient_oper('servers', 'find', proj_name,
                                           id=vm_back_ref['uuid'])
                if vm.name == instance_name:
                    exists = True
                    break

            if exists:
                vm_uuid = vm_back_ref['uuid']
            else:
                vm = self._create_svc_vm(instance_name, image_name, nics,
                                         flavor, st_obj, si_obj, avail_zone)
                if vm is None:
                    continue
                vm_uuid = vm.id

            # store vm, instance in db; use for linking when VM is up
            row_entry = {}
            row_entry['si_fq_str'] = si_obj.get_fq_name_str()
            row_entry['instance_name'] = instance_name
            row_entry['instance_type'] = svc_info.get_vm_instance_type()
            self.db.virtual_machine_insert(vm_uuid, row_entry)

            # uve trace
            self.logger.uve_svc_instance(si_obj.get_fq_name_str(),
                status='CREATE', vm_uuid=vm.id,
                st_name=st_obj.get_fq_name_str())

    def delete_service(self, vm_uuid, proj_name=None):
        try:
            self.novaclient_oper('servers', 'find', proj_name,
                id=vm_uuid).delete()
        except nc_exc.NotFound:
            raise KeyError

    def _novaclient_get(self, proj_name, reauthenticate=False):
        # return cache copy when reauthenticate is not requested
        if not reauthenticate:
            client = self._nova.get(proj_name)
            if client is not None:
                return client

        self._nova[proj_name] = nc.Client(
            '2', username=self._args.admin_user, project_id=proj_name,
            api_key=self._args.admin_password,
            region_name=self._args.region_name, service_type='compute',
            auth_url='http://' + self._args.auth_host + ':5000/v2.0')
        return self._nova[proj_name]

    def novaclient_oper(self, resource, oper, proj_name, **kwargs):
        n_client = self._novaclient_get(proj_name)
        try:
            resource_obj = getattr(n_client, resource)
            oper_func = getattr(resource_obj, oper)
            return oper_func(**kwargs)
        except nc_exc.Unauthorized:
            n_client = self._novaclient_get(proj_name, True)
            oper_func = getattr(n_client, oper)
            return oper_func(**kwargs)


class NetworkNamespaceManager(InstanceManager):

    def create_service(self, st_obj, si_obj):
        si_props = si_obj.get_service_instance_properties()
        st_props = st_obj.get_service_template_properties()
        if st_props is None:
            self.logger.log("Cannot find service template associated to "
                             "service instance %s" % si_obj.get_fq_name_str())
            return
        '''TODO: add check for lb and snat. Need a new class to drive this
        if (st_props.get_service_type() != svc_info.get_snat_service_type()):
            self.logger.log("Only service type 'source-nat' is supported "
                             "with 'network-namespace' service "
                             "virtualization type")
            return
        '''

        # populate nic information
        nics = self._get_nic_info(si_obj, si_props, st_props)

        # Create virtual machines, associate them to the service instance and
        # schedule them to different virtual routers
        if si_props.get_scale_out():
            max_instances = si_props.get_scale_out().get_max_instances()
        else:
            max_instances = 1
        for inst_count in range(0, max_instances):
            # Create a virtual machine
            instance_name = si_obj.name + '_' + str(inst_count + 1)
            proj_fq_name = si_obj.get_parent_fq_name()
            vm_name = "__".join(proj_fq_name + [instance_name])
            try:
                vm_obj = self._vnc_lib.virtual_machine_read(fq_name=[vm_name])
                self.logger.log("Info: VM %s already exists" % (vm_name))
            except NoIdError:
                vm_obj = VirtualMachine(vm_name)
                self._vnc_lib.virtual_machine_create(vm_obj)
                self.logger.log("Info: VM %s created" % (vm_name))

            vm_obj.set_service_instance(si_obj)
            self._vnc_lib.virtual_machine_update(vm_obj)
            self.logger.log("Info: VM %s updated with SI %s" %
                             (instance_name, si_obj.get_fq_name_str()))

            # Create virtual machine interfaces with an IP on networks
            for nic in nics:
                vmi_obj = self._create_svc_vm_port(nic, instance_name, st_obj,
                                                   si_obj)
                vmi_obj.set_virtual_machine(vm_obj)
                self._vnc_lib.virtual_machine_interface_update(vmi_obj)
                self.logger.log("Info: VMI %s updated with VM %s" %
                                 (vmi_obj.get_fq_name_str(), instance_name))

            # store NetNS instance in db use for linking when NetNS
            # is up. If the 'vrouter_name' key does not exist that means the
            # NetNS was not scheduled to a vrouter
            row_entry = {}
            row_entry['si_fq_str'] = si_obj.get_fq_name_str()
            row_entry['instance_name'] = instance_name
            row_entry['instance_type'] = svc_info.get_netns_instance_type()

            # Associate instance on the scheduled vrouter
            chosen_vr_fq_name = self.vrouter_scheduler.schedule(si_obj.uuid,
                                                                vm_obj.uuid)
            if chosen_vr_fq_name:
                row_entry['vrouter_name'] = chosen_vr_fq_name[-1]
                self.logger.log("Info: VRouter %s updated with VM %s" %
                                 (':'.join(chosen_vr_fq_name), instance_name))

            self.db.virtual_machine_insert(vm_obj.uuid, row_entry)

            # uve trace
            if chosen_vr_fq_name:
                self.logger.uve_svc_instance(si_obj.get_fq_name_str(),
                    status='CREATE', vm_uuid=vm_obj.uuid,
                    st_name=st_obj.get_fq_name_str(),
                    vr_name=':'.join(chosen_vr_fq_name))
            else:
                self.logger.uve_svc_instance(si_obj.get_fq_name_str(),
                    status='CREATE', vm_uuid=vm_obj.uuid,
                    st_name=st_obj.get_fq_name_str())

    def delete_service(self, vm_uuid, proj_name=None):
        try:
            vm_obj = self._vnc_lib.virtual_machine_read(id=vm_uuid)
        except NoIdError:
            raise KeyError
        for vmi in vm_obj.get_virtual_machine_interface_back_refs() or []:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                id=vmi['uuid'])
            for ip in vmi_obj.get_instance_ip_back_refs() or []:
                self._vnc_lib.instance_ip_delete(id=ip['uuid'])
            self._vnc_lib.virtual_machine_interface_delete(id=vmi['uuid'])
        for vr in vm_obj.get_virtual_router_back_refs() or []:
            vr_obj = self._vnc_lib.virtual_router_read(id=vr['uuid'])
            vr_obj.del_virtual_machine(vm_obj)
            self._vnc_lib.virtual_router_update(vr_obj)
            self.logger.log("UPDATE: Svc VM %s deleted from VR %s" %
                (vm_obj.get_fq_name_str(), vr_obj.get_fq_name_str()))
        self._vnc_lib.virtual_machine_delete(id=vm_obj.uuid)

    def _create_snat_vn(self, proj_obj, si_obj, si_props, vn_fq_name_str):
        # SNAT NetNS use a dedicated network (non shared vn)
        vn_name = 'svc-snat-%s' % si_obj.name
        vn_fq_name = proj_obj.get_fq_name() + [vn_name]
        try:
            vn_id = self._vnc_lib.fq_name_to_id('virtual-network',
                                                vn_fq_name)
        except NoIdError:
            snat_cidr = svc_info.get_snat_left_subnet()
            vn_id = self._create_svc_vn(vn_name, snat_cidr, proj_obj)

        if vn_fq_name_str != ':'.join(vn_fq_name):
            si_props.set_left_virtual_network(':'.join(vn_fq_name))
            si_obj.set_service_instance_properties(si_props)
            self._vnc_lib.service_instance_update(si_obj)
            self.logger.log("Info: SI %s updated with left vn %s" %
                             (si_obj.get_fq_name_str(), vn_fq_name_str))

        return vn_id
