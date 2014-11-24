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

from cfgm_common import svc_info
from vnc_api.vnc_api import *
from instance_manager import InstanceManager

class VirtualMachineManager(InstanceManager):

    def _create_svc_vm(self, instance_name, image_name, nics,
                       flavor_name, st_obj, si_obj, avail_zone):

        proj_name = si_obj.get_parent_fq_name()[-1]
        if flavor_name:
            flavor = self._nc.oper('flavors', 'find', proj_name,
                                   name=flavor_name)
        else:
            flavor = self._nc.oper('flavors', 'find', proj_name, ram=4096)
        if not flavor:
            return

        image = self._nc.oper('images', 'find', proj_name, name=image_name)
        if not image:
            return

        # create port
        nics_with_port = []
        for nic in nics:
            nic_with_port = {}
            vmi_obj = self._create_svc_vm_port(nic, instance_name,
                                               st_obj, si_obj)
            nic_with_port['port-id'] = vmi_obj.get_uuid()
            nics_with_port.append(nic_with_port)

        # launch vm
        self.logger.log('Launching VM : ' + instance_name)
        nova_vm = self._nc.oper('servers', 'create', proj_name,
            name=instance_name, image=image,
            flavor=flavor, nics=nics_with_port,
            availability_zone=avail_zone)
        nova_vm.get()
        self.logger.log('Created VM : ' + str(nova_vm))

        # create vnc VM object and link to SI
        try:
            proj_obj = self._vnc_lib.project_read(
                fq_name=si_obj.get_parent_fq_name())
            vm_obj = VirtualMachine(nova_vm.id)
            vm_obj.uuid = nova_vm.id
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=nova_vm.id)

        vm_obj.add_service_instance(si_obj)
        self._vnc_lib.virtual_machine_update(vm_obj)
        self.logger.log("Info: VM %s updated SI %s" %
            (vm_obj.get_fq_name_str(), si_obj.get_fq_name_str()))

        return nova_vm

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
        self.db.service_instance_insert(si_obj.get_fq_name_str(),
                                        {'max-instances': str(max_instances),
                                         'state': 'launching'})
        instances = []
        for inst_count in range(0, max_instances):
            instance_name = self._get_instance_name(si_obj, inst_count)
            si_info = self.db.service_instance_get(si_obj.get_fq_name_str())
            prefix = self.db.get_vm_db_prefix(inst_count)
            if prefix + 'name' not in si_info.keys():
                vm = self._create_svc_vm(instance_name, image_name, nics,
                                         flavor, st_obj, si_obj, avail_zone)
                if not vm:
                    continue
                vm_uuid = vm.id
                state = 'pending'
            else:
                vm = self._nc.oper('servers', 'find', proj_name,
                                   id=si_info[prefix + 'uuid'])
                if not vm:
                    continue
                vm_uuid = si_info[prefix + 'uuid']
                state = 'active'

            # store vm, instance in db; use for linking when VM is up
            vm_db_entry = self._set_vm_db_info(inst_count, instance_name,
                                               vm_uuid, state)
            self.db.service_instance_insert(si_obj.get_fq_name_str(),
                                            vm_db_entry)
            instances.append({'uuid': vm_uuid})

        self.db.service_instance_insert(si_obj.get_fq_name_str(),
                                        {'state': 'active'})
        # uve trace
        self.logger.uve_svc_instance(si_obj.get_fq_name_str(),
            status='CREATE', vms=instances,
            st_name=st_obj.get_fq_name_str())

    def delete_service(self, si_fq_str, vm_uuid, proj_name=None):
        self.db.remove_vm_info(si_fq_str, vm_uuid)

        try:
            self._vnc_lib.virtual_machine_delete(id=vm_uuid)
        except (NoIdError, RefsExistError):
            pass

        vm = self._nc.oper('servers', 'find', proj_name, id=vm_uuid)
        if not vm:
            raise KeyError

        try:
            vm.delete()
        except Exception:
            pass

    def check_service(self, si_obj, proj_name=None):
        status = 'ACTIVE'
        vm_list = {}
        vm_back_refs = si_obj.get_virtual_machine_back_refs()
        for vm_back_ref in vm_back_refs or []:
            vm = self._nc.oper('servers', 'find', proj_name,
                               id=vm_back_ref['uuid'])
            if vm:
                vm_list[vm.name] = vm
            else:
                try:
                    self._vnc_lib.virtual_machine_delete(id=vm_back_ref['uuid'])
                except (NoIdError, RefsExistError):
                    pass

        # check status of VMs
        si_props = si_obj.get_service_instance_properties()
        max_instances = si_props.get_scale_out().get_max_instances()
        for inst_count in range(0, max_instances):
            instance_name = self._get_instance_name(si_obj, inst_count)
            if instance_name not in vm_list.keys():
                status = 'ERROR'
            elif vm_list[instance_name].status == 'ERROR':
                try:
                    self.delete_service(si_obj.get_fq_name_str(),
                        vm_list[instance_name].id, proj_name)
                except KeyError:
                    pass
                status = 'ERROR'

        # check change in instance count
        if vm_back_refs and (max_instances > len(vm_back_refs)):
            status = 'ERROR'
        elif vm_back_refs and (max_instances < len(vm_back_refs)):
            for vm_back_ref in vm_back_refs:
                try:
                    self.delete_service(si_obj.get_fq_name_str(),
                        vm_back_ref['uuid'], proj_name)
                except KeyError:
                    pass
            status = 'ERROR'

        return status

    def update_static_routes(self, si_obj):
        # get service instance interface list
        si_props = si_obj.get_service_instance_properties()
        si_if_list = si_props.get_interface_list()
        if not si_if_list:
            return

        st_list = si_obj.get_service_template_refs()
        fq_name = st_list[0]['to']
        st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)
        st_props = st_obj.get_service_template_properties()
        st_if_list = st_props.get_interface_type()

        for idx in range(0, len(si_if_list)):
            si_if = si_if_list[idx]
            static_routes = si_if.get_static_routes()
            if not static_routes:
                static_routes = {'route':[]}

            # update static routes
            try:
                rt_fq_name = self._get_if_route_table_name(
                    st_if_list[idx].get_service_interface_type(),
                    si_obj)
                rt_obj = self._vnc_lib.interface_route_table_read(
                    fq_name=rt_fq_name)
                rt_obj.set_interface_route_table_routes(static_routes)
                self._vnc_lib.interface_route_table_update(rt_obj)
            except NoIdError:
                pass
