import uuid
from vnc_api.vnc_api import *
from instance_manager import InstanceManager
from config_db import (
    VirtualMachineSM,
    ServiceApplianceSetSM,
    ServiceApplianceSM,
    PhysicalInterfaceSM,
    ServiceInstanceSM)
from cfgm_common import svc_info


class PhysicalServiceManager(InstanceManager):

    def create_service(self, st, si):
        if not self.validate_network_config(st, si):
            return
        # get service appliances from service template
        sa_set = st.service_appliance_set
        if not sa_set:
            self.logger.log_error("Can't find service appliances set")
            return
        service_appliance_set = ServiceApplianceSetSM.get(sa_set)
        service_appliances = service_appliance_set.service_appliances

        # validation
        if not service_appliances:
            self.logger.log_error("Can't find service appliances")
            return

        service_appliances = list(service_appliances)

        si_obj = ServiceInstanceSM.get(si.uuid)

        # create a fake VM for the schmea transfer to use
        vm_list = list(si_obj.virtual_machines)
        for vm_uuid in vm_list:
            vm = VirtualMachineSM.get(vm_uuid)
            if not vm:
                continue
            if (vm.index + 1) > si.max_instances:
                self.delete_service(vm)
                vm_list.pop()
                continue
            vm_list[vm.index] = vm_uuid

        if si.max_instances > len(service_appliances):
            self.logger.log_info(
                "There are not enough Service appliance \
                    for that Service instance "+si.uuid)
            return
        for idx, sa_uuid in enumerate(service_appliances):
            vm_uuid = ""
            if idx < len(vm_list):
                vm_uuid = vm_list[idx]
            else:
                vm_uuid = str(uuid.uuid4())
            vm_obj = self.link_si_to_vm(si, st, idx, vm_uuid)
            instance_name = self._get_instance_name(si, idx)
            si.state = 'launching'
            sa = ServiceApplianceSM.get(sa_uuid)
            # Create VMI
            pi_set = {}
            for pi_uuid in sa.physical_interfaces:
                pi_obj = PhysicalInterfaceSM.get(pi_uuid)
                if pi_obj.interface_type:
                    pi_set[pi_obj.interface_type] = pi_obj
            for i in range(0, len(si.vn_info)):
                nic = si.vn_info[i]
                pi_obj = pi_set.get(nic['type'],None)
                if not pi_obj:
                    return
                vmi_obj = self._create_svc_vm_port(nic,
                                                   instance_name, si, st,
                                                   vm_obj = vm_obj,
                                                   pi=pi_obj,
                                                   instance_id=idx)
        si.state = "active"

    def delete_service(self, vm):
        self.cleanup_pi_connection(vm.virtual_machine_interfaces)
        self.cleanup_svc_vm_ports(vm.virtual_machine_interfaces)
        try:
            self._vnc_lib.virtual_machine_delete(id=vm.uuid)
        except NoIdError:
            pass

    def cleanup_pi_connection(self, vmi_list):
        for vmi_id in vmi_list:
            try:
                vmi = VirtualMachineInterfaceSM.get(vmi_id)
                self._vnc_lib.ref_update('virtual-machine-interface',
                                         vmi.uuid,
                                         'physical_interface_refs',
                                         vmi.physical_interface,
                                         None,
                                         'DELETE')
            except:
                pass

    def check_service(self, si):
        return True
