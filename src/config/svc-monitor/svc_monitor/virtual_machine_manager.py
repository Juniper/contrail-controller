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

from novaclient import exceptions as nc_exc

class VirtualMachineManager(InstanceManager):

    def _create_service_vm(self, instance_index, si, st):
        proj_name = si.fq_name[-2]
        try:
            if si.flavor:
                flavor = self._nc.oper('flavors', 'find', proj_name,
                                       name=si.flavor)
            else:
                flavor = self._nc.oper('flavors', 'find', proj_name, ram=4096)
        except nc_exc.NotFound:
            flavor = None
        if not flavor:
            self.logger.error("Flavor not found %s" %
                ((':').join(st.fq_name)))
            return None

        try:
            image = self._nc.oper('images', 'find', proj_name, name=si.image)
        except nc_exc.NotFound:
            image = None
        if not image:
            self.logger.error("Image not found %s" % si.image)
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
        idx_str = "%(#)03d" % {'#': (instance_index + 1)}
        nova_vm_name = si.name + idx_str
        self.logger.info('Launching VM : ' + nova_vm_name)
        nova_vm = self._nc.oper('servers', 'create', proj_name,
            name=nova_vm_name, image=image,
            flavor=flavor, nics=nics_with_port,
            availability_zone=si.availability_zone)
        if not nova_vm:
            self.logger.error("Nova vm create failed %s" % nova_vm_name)
            return None

        nova_vm.get()
        self.logger.info('Created VM : ' + str(nova_vm))

        # link si and vm
        self.link_si_to_vm(si, st, instance_index, nova_vm.id)
        return nova_vm.id

    def _validate_nova_objects(self, st, si):
        # check image and flavor
        si.flavor = st.params.get('flavor', None)
        si.image = st.params.get('image_name', None)
        if not si.image:
            self.logger.error("Image not present in %s" %
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
        vm_list = [None] * si.max_instances
        vm_id_list = list(si.virtual_machines)
        for vm_id in vm_id_list:
            vm = VirtualMachineSM.get(vm_id)
            if not vm:
                continue
            if (vm.index + 1) > si.max_instances:
                self.delete_service(vm)
                continue
            vm_list[vm.index] = vm

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

    def delete_service(self, vm):
        # instance ip delete
        vmi_list = []
        for vmi_id in vm.virtual_machine_interfaces:
            vmi_list.append(vmi_id)
        self.cleanup_svc_vm_ports(vmi_list)

        # nova vm delete
        nova_vm_deleted = False
        proj_name = vm.proj_fq_name[-1]
        try:
            nova_vm = self._nc.oper('servers', 'get', proj_name, id=vm.uuid)
        except nc_exc.NotFound:
            nova_vm_deleted = True
            nova_vm = None

        if nova_vm:
            try:
                nova_vm.delete()
                nova_vm_deleted = True
            except Exception as e:
                self.logger.error("%s nova delete failed with error %s" %
                    (vm.uuid, str(e)))

        if nova_vm_deleted:
            try:
                self._vnc_lib.virtual_machine_delete(id=vm.uuid)
            except NoIdError:
                pass
            except RefsExistError:
                self.logger.error("%s vm delete RefsExist" % (vm.uuid))

    def check_service(self, si):
        vm_id_list = list(si.virtual_machines)
        for vm_id in vm_id_list:
            try:
                vm = self._nc.oper('servers', 'get', si.proj_name, id=vm_id)
            except nc_exc.NotFound:
                vm = None
            if vm and vm.status == 'ERROR':
                try:
                    vm.delete()
                except Exception:
                    pass

        return True
