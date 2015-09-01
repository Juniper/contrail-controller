import uuid
from vnc_api.vnc_api import *
from instance_manager import InstanceManager
from config_db import ServiceApplianceSetSM, ServiceApplianceSM, PhysicalInterfaceSM
from cfgm_common import svc_info

class PhysicalMachineManager(InstanceManager):


    def create_service(self, st, si):
        if not self.validate_network_config(st, si):
            return
        #get service appliances from service template
        st_sets = list(st.service_appliance_sets)
        service_appliance_set = ServiceApplianceSetSM.get(st_sets[0])
        service_appliances = service_appliance_set.service_appliances

        #validation
        if service_appliances == None:
            self.logger.log_error("Can't find service appliances")
            return

        service_appliances = list(service_appliances)
        counter = 0

        si_obj = self._vnc_lib.service_instance_read(id=si.uuid)
        vmi_si_refs = si_obj.get_virtual_machine_interface_refs()
        vmi_si_set = set()
        si_vmi_added = False

        #create a fake VM for the schmea transfer to use
        vm_si_refs = si_obj.get_virtual_machine_back_refs()
        vm_id = ""
        if vm_si_refs and len(vm_si_refs)>0:
            vm_id = vm_si_refs[0]['uuid']
        else:
            vm_id = str(uuid.uuid4())
        self.link_si_to_vm(si, st, 0, vm_id)
        vm_obj =self._vnc_lib.virtual_machine_read(id=vm_id)

        if vmi_si_refs:
            for vmi_si_ref in vmi_si_refs:
                vmi_si_set.add(vmi_si_ref['uuid'])
        for idx in range(si.max_instances):
            instance_name = self._get_instance_name(si, idx)
            if idx+1 > len(service_appliances):
                sa_uuid = service_appliances[counter]
                counter = counter + 1
                if counter + 1 > len(service_appliances):
                    counter = 0
            else:
                sa_uuid = service_appliances[idx]

            si.state = 'launching'
            sa = ServiceApplianceSM.get(sa_uuid)
            #Create VMI
            ports = []
            for nic in si.vn_info:
                vmi_obj = self._create_svc_vm_port(nic, instance_name, si, st)
                if vmi_obj.uuid not in vmi_si_set:
                    vmi_si_obj = ServiceInstanceVirtualMachineInterfaceType()
                    int_uuid = str(si.uuid).replace("-","")
                    int_uuid = int(int_uuid,16)
                    vmi_si_obj.instance_id = int_uuid
                    vmi_si_obj.interface_type = nic['type']
                    si_obj.add_virtual_machine_interface(vmi_obj,vmi_si_obj)
                    si_vmi_added = True
                vmi_obj.add_virtual_machine(vm_obj)
                ports.append(vmi_obj)

            #connects the VMI with the PI
            pi_uuid_set = list(sa.physical_interfaces)
            for i in range(0,len(ports)):
                if i <= len(pi_uuid_set):
                    pi = self._vnc_lib.physical_interface_read(id=pi_uuid_set[i])
                    vmi = ports[i]
                    vmi.add_physical_interface(pi)
                    self._vnc_lib.virtual_machine_interface_update(vmi)

        #fire change of the SI
        if si_vmi_added:
            self._vnc_lib.service_instance_update(si_obj)
            si_vmi_added = False
        si.state = "active"



    def delete_service(self, vm):
        #TODO
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
                        pi = self._vnc_lib.physical_interface_read(pi_ref['uuid'])
                        vmi.del_physical_interface(pi)
                        self._vnc_lib.virtual_machine_interface_update(vmi)
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
        vm_id_list = list(si.virtual_machines)
        '''
        for vm_id in vm_id_list:
            vm = self._nc.oper('servers', 'get', si.proj_name, id=vm_id)
            if vm and vm.status == 'ERROR':
                try:
                    vm.delete()
                except Exception:
                    pass
        '''
        return True

