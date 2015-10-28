import uuid
from vnc_api.vnc_api import *
from instance_manager import InstanceManager
from config_db import (
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
        counter = 0

        si_obj = ServiceInstanceSM.get(si.uuid)
        vmi_si_set = si_obj.virtual_machine_interfaces
        si_vmi_added = False

        # create a fake VM for the schmea transfer to use
        vm_si_refs = list(si_obj.virtual_machines)
        vm_id = ""
        if vm_si_refs:
            vm_id = vm_si_refs[0]
        else:
            vm_id = str(uuid.uuid4())
        self.link_si_to_vm(si, st, 0, vm_id)
        vm_obj = self._vnc_lib.virtual_machine_read(id=vm_id)

        if si.max_instances > len(service_appliances):
            self.logger.log_info(
                "There are not enough Service appliance \
                    for that Service instance")
            return
        idx = 0
        for idx, sa_uuid in enumerate(service_appliances):
            instance_name = self._get_instance_name(si, idx)
            si.state = 'launching'
            sa = ServiceApplianceSM.get(sa_uuid)
            # Create VMI
            pi_uuid_set = list(sa.physical_interfaces)
            for i in range(0, len(si.vn_info)):
                nic = si.vn_info[i]
                vmi_obj = self._create_svc_vm_port(nic,
                                                   instance_name, si, st,
                                                   pi=pi_uuid_set[i],
                                                   instance_id=idx)
                vmi_obj.add_virtual_machine(vm_obj)
                self._vnc_lib.virtual_machine_interface_update(vmi_obj)
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
                try:
                    self._vnc_lib.ref_update('virtual-machine-interface',
                                             vmi.uuid,
                                             'physical_interface_refs',
                                             vmi.physical_interface,
                                             None,
                                             'DELETE')
                except:
                    pass
            except:
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

        vn_fq_name = si.fq_name[:-1] + [service_vn_name]
        try:
            vn_id = self._vnc_lib.fq_name_to_id(
                'virtual-network', vn_fq_name)
        except NoIdError:
            vn_id = self.create_service_vn(service_vn_name,
                                           service_vn_subnet, si.fq_name[:-1])

        return vn_id

    def check_service(self, si):
        return True
