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
from config_db import VirtualMachineSM

class VirtualMachineManager(InstanceManager):

    def _create_service_vm(self, instance_index, si, st):
        proj_name = si.fq_name[-2]
        if si.flavor:
            flavor = self._nc.oper('flavors', 'find', proj_name,
                                   name=si.flavor)
        else:
            flavor = self._nc.oper('flavors', 'find', proj_name, ram=4096)
        if not flavor:
            return None

        image = self._nc.oper('images', 'find', proj_name, name=si.image)
        if not image:
            return None

        instance_name = self._get_instance_name(si, instance_index)

        # create port
        nics_with_port = []
        for nic in si.vn_info:
            nic_with_port = {}
            vmi_obj = self._create_svc_vm_port(nic, instance_name, si, st)
            nic_with_port['port-id'] = vmi_obj.get_uuid()
            nics_with_port.append(nic_with_port)

        # launch vm
        self.logger.log('Launching VM : ' + instance_name)
        nova_vm = self._nc.oper('servers', 'create', proj_name,
            name=instance_name, image=image,
            flavor=flavor, nics=nics_with_port,
            availability_zone=si.availability_zone)
        if not nova_vm:
            self.cleanup_svc_vm_ports(nics_with_port)
            return None
        else:
            nova_vm.get()
            self.logger.log('Created VM : ' + str(nova_vm))

        # link si and vm
        try:
            vm_obj = VirtualMachine(nova_vm.id)
            vm_obj.uuid = nova_vm.id
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=nova_vm.id)

        si_obj = self._vnc_lib.service_instance_read(id=si.uuid)
        vm_obj.add_service_instance(si_obj)
        vm_obj.set_display_name(instance_name + '__' + st.virtualization_type)
        self._vnc_lib.virtual_machine_update(vm_obj)
        self.logger.log("Info: VM %s updated SI %s" %
            (vm_obj.get_fq_name_str(), si_obj.get_fq_name_str()))

        return nova_vm.id

    def _check_create_service_vn(self, itf_type, si):
        vn_id = None

        # search or create shared vn
        funcname = "get_" + itf_type + "_vn_name"
        func = getattr(svc_info, funcname)
        service_vn_name = func()
        funcname = "get_" + itf_type + "_vn_subnet"
        func = getattr(svc_info, funcname)
        service_vn_subnet = func()

        vn_fq_name = si.fq_name[:-1] + [service_vn_name]
        try:
            vn_id = self._vnc_lib.fq_name_to_id(
                'virtual-network', vn_fq_name)
        except NoIdError:
            vn_id = self.create_service_vn(service_vn_name,
                service_vn_subnet, si.fq_name[:-1])

        return vn_id

    def _validate_nova_objects(self, st, si):
        # check image and flavor
        si.flavor = st.params.get('flavor', None)
        si.image = st.params.get('image_name', None)
        if not (si.flavor or si.image):
            self.logger.log("ERROR: Image/flavor not present in %s" %
                ((':').join(st.fq_name)))
            return False

        # get availability zone
        if st.params.get('availability_zone_enable', None):
            si.availability_zone = si.params.get('availability_zone')
        elif self._args.availability_zone:
            si.availability_zone = self._args.availability_zone

        return True

    def create_service(self, st, si):
        if not self._validate_nova_objects(st, si):
            return
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
            if vm_list[index]:
                vm_uuid = vm_list[index].uuid
            else:
                vm_uuid = self._create_service_vm(index, si, st)
                if not vm_uuid:
                    continue

            instances.append({'uuid': vm_uuid})

        # update static routes
        self.update_static_routes(si)

        # uve trace
        si.state = 'active'
        self.logger.uve_svc_instance(":".join(si.fq_name), status='CREATE',
            vms=instances, st_name=st.name)

    def delete_service(self, vm_uuid, proj_name=None):
        vm = self._nc.oper('servers', 'get', proj_name, id=vm_uuid)
        if vm:
            try:
                vm.delete()
            except Exception:
                pass

    def check_service(self, si):
        for vm_id in si.virtual_machines:
            vm = self._nc.oper('servers', 'get', si.proj_name, id=vm_id)
            if vm and vm.status == 'ERROR':
                try:
                    vm.delete()
                except Exception:
                    pass

        return True
