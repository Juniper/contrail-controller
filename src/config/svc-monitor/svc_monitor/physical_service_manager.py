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
        vmi_si_refs = list(si_obj.virtual_machine_interfaces)
        vmi_si_set = set()
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

        if vmi_si_refs:
            for vmi_si_ref in vmi_si_refs:
                vmi_si_set.add(vmi_si_ref)
        if si.max_instances > len(service_appliances):
            self.logger.log_info(
                "There are not enough Service appliance \
                    for that Service instance")
            return
        idx = 0
        si_vnc = self._vnc_lib.service_instance_read(id=si.uuid)
        for idx, sa_uuid in enumerate(service_appliances):
            instance_name = self._get_instance_name(si, idx)
            si.state = 'launching'
            sa = ServiceApplianceSM.get(sa_uuid)
            # Create VMI
            pi_uuid_set = list(sa.physical_interfaces)
            for nic in si.vn_info:
                vmi_obj = self._create_svc_vm_port(nic, 
                    instance_name, si, st, pi=pi_uuid_set[idx])
                if vmi_obj.uuid not in vmi_si_set:
                    vmi_si_obj = ServiceInstanceVirtualMachineInterfaceType()
                    vmi_si_obj.instance_id = idx
                    vmi_si_obj.interface_type = nic['type']
                    si_vnc.add_virtual_machine_interface(vmi_obj, vmi_si_obj)
                    si_vmi_added = True
                vmi_obj.add_virtual_machine(vm_obj)
                self._vnc_lib.virtual_machine_interface_update(vmi_obj)   
        # fire change of the SI
        if si_vmi_added:
            self._vnc_lib.service_instance_update(si_vnc)
            si_vmi_added = False
        si.state = "active"

    def delete_service(self, vm):
        # TODO
        vmi_list = []
        for vmi_id in vm.virtual_machine_interfaces:
            vmi_list.append(vmi_id)
        self.cleanup_pi_connection(vmi_list)
        self.cleanup_svc_vm_ports(vmi_list)
        try:
            self._vnc_lib.virtual_machine_delete(id=vm.uuid)
        except NoIdError:
            pass

    def cleanup_pi_connection(self, vmi_list):
        for vmi_id in vmi_list:
            try:
                vmi = self._vnc_lib.virtual_machine_interface_read(vmi_id)
                pi_refs = vmi.get_physical_interface_refs()
                for pi_ref in pi_refs:
                    try:
                        self._vnc_lib.ref_update('virtual-machine-interface',
                                                 vmi.uuid,
                                                 'physical_interface_refs',
                                                 pi_ref['uuid'],
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
