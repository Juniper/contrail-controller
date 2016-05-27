import uuid
from vnc_api.vnc_api import *
from instance_manager import InstanceManager
from config_db import (
    VirtualMachineSM,
    VirtualMachineInterfaceSM,
    ServiceApplianceSetSM,
    ServiceApplianceSM,
    PhysicalInterfaceSM,
    ServiceInstanceSM,
    ServiceTemplateSM,
    PortTupleSM)
from cfgm_common import svc_info


class PhysicalServiceManager(InstanceManager):

    def create_service(self, st, si):
        if not self.validate_network_config(st, si):
            return
        # if service already inited do nothing
        if self.check_service(si):
            return

        # get service appliances from service template
        if not st.service_appliance_set:
            self.logger.error("Can't find service appliances set")
            return
        sa_set_obj = ServiceApplianceSetSM.get(st.service_appliance_set)
        sa_list = list(sa_set_obj.service_appliances)

        # validation
        if not sa_list:
            self.logger.error("Can't find service appliances")
            return

        #clean all existed staff before create new
        self.clean_service(si)
        # create a fake VM for the schmea transfer to use
        vm_list = [None]*si.max_instances

        # get the port-tuple
        pt_list = [None]*si.max_instances
        if si.max_instances > len(sa_list):
            self.logger.info(
                "There are not enough Service appliance \
                    for that Service instance "+si.uuid)
            return
        for idx, sa_uuid in enumerate(sa_list):
            if idx > si.max_instances:
                return

            vm_uuid = vm_list[idx]
            if not vm_uuid:
                vm_uuid = str(uuid.uuid4())
            vm_obj = self.link_si_to_vm(si, st, idx, vm_uuid)

            pt_uuid = pt_list[idx]
            if not pt_uuid:
                pt_uuid = str(uuid.uuid4())
            pt_obj = self.create_port_tuple(si, st, idx, pt_uuid)

            instance_name = self._get_instance_name(si, idx)
            si.state = 'launching'
            sa = ServiceApplianceSM.get(sa_uuid)
            for nic in si.vn_info:
                pi_uuid = sa.physical_interfaces.get(nic['type'], None)
                pi_obj = PhysicalInterfaceSM.get(pi_uuid)
                if not pi_obj:
                    return
                vmi_obj = self._create_svc_vm_port(nic,
                                                   instance_name, si, st,
                                                   vm_obj=vm_obj,
                                                   pi=pi_obj,
                                                   pt=pt_obj)
        si.state = "active"

    def delete_service(self, vm):
        self.delete_vm(vm)

    def clean_service(self, si):
        self.cleanup_si_iip_connections(si)
        vm_uuid_list = list(si.virtual_machines)
        for vm_uuid in vm_uuid_list:
            vm_obj = VirtualMachineSM.get(vm_uuid)
            if vm_obj:
                self.delete_vm(vm_obj)

    def delete_vm(self,vm):
        if vm.virtual_machine_interfaces:
            vmi_list = list(vm.virtual_machine_interfaces)
            pt_uuid = VirtualMachineInterfaceSM.get(vmi_list[0]).port_tuple
            self.cleanup_pi_connections(vmi_list)
            self.cleanup_svc_vm_ports(vmi_list)
            try:
                self._vnc_lib.port_tuple_delete(id=pt_uuid)
                PortTupleSM.delete(pt_uuid)
            except NoIdError:
                pass
        try:
            self._vnc_lib.virtual_machine_delete(id=vm.uuid)
            VirtualMachineSM.delete(vm.uuid)
        except NoIdError:
            pass

    def cleanup_si_iip_connections(self,si):
        iip_list = list(si.instance_ips)
        for iip_id in iip_list:
            try:
                self._vnc_lib.ref_update('service-instance',
                            si.uuid,
                            'instance-ip',
                            iip_id,
                            None,
                            'DELETE')
            except:
                pass

    def cleanup_pi_connections(self, vmi_list):
        for vmi_id in vmi_list:
            try:
                vmi = VirtualMachineInterfaceSM.get(vmi_id)
                self._vnc_lib.ref_update('virtual-machine-interface',
                                         vmi.uuid,
                                         'physical-interface',
                                         vmi.physical_interface,
                                         None,
                                         'DELETE')
                PhysicalInterfaceSM.locate(vmi.physical_interface)
            except:
                pass

    def check_service(self, si):
        if si.max_instances>len(si.port_tuples):
            return False

        pt_list = list(si.port_tuples)
        pi_list = []
        all_possible_pi=[]

        for pt_uuid in pt_list:
            pt_obj = PortTupleSM.get(pt_uuid)
            for vmi_uuid in pt_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceSM.get(vmi_uuid)
                pi_list.append(vmi_obj.physical_interface)

        st_obj = ServiceTemplateSM.get(si.service_template)
        if not st_obj.service_appliance_set:
            return False

        sa_set_obj = ServiceApplianceSetSM.get(st_obj.service_appliance_set)
        for sa_uuid in sa_set_obj.service_appliances:
            sa_obj = ServiceApplianceSM.get(sa_uuid)
            for key in sa_obj.physical_interfaces:
                all_possible_pi.append(sa_obj.physical_interfaces[key])

        if not pi_list and all_possible_pi and si.max_instances>0:
            return False

        if not all_possible_pi and pi_list:
            return False

        for pi_uuid in pi_list:
            if not pi_uuid in all_possible_pi:
                return False

        return True
