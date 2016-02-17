# @author: Bartlomiej Biernacki

from .instance_manager import VRouterHostedManager
from vnc_api.vnc_api import *
from .config_db import VirtualRouterSM, VirtualMachineSM

# Manager for service instances (Docker or KVM) hosted on selected vrouter
class VRouterInstanceManager(VRouterHostedManager):
    def _associate_vrouter(self, si, vm):
        vrouter_name = None
        vr_obj = None
        vm_obj = VirtualMachine()
        vm_obj.uuid = vm.uuid
        vm_obj.fq_name = vm.fq_name

        if vm.virtual_router:
            vr = VirtualRouterSM.get(vm.virtual_router)
            if si.vr_id == vr.uuid:
                vrouter_name = vr.name
            else:
                vr_obj = VirtualRouter()
                vr_obj.uuid = vr.uuid
                vr_obj.fq_name = vr.fq_name
                vr_obj.del_virtual_machine(vm_obj)
                self._vnc_lib.virtual_router_update(vr_obj)
                self.logger.info("vm %s deleted from vrouter %s" %
                    (vm_obj.get_fq_name_str(), vr_obj.get_fq_name_str()))
                vm.virtual_router = None

        if not vm.virtual_router:
            vr = VirtualRouterSM(si.vr_id)
            vr_obj = VirtualRouter()
            vr_obj.uuid = vr.uuid
            vr_obj.fq_name = vr.fq_name
            vr_obj.add_virtual_machine(vm_obj)
            self._vnc_lib.virtual_router_update(vr_obj)
            self.logger.info("vrouter %s updated with vm %s" %
                (':'.join(vr_obj.get_fq_name()), vm.name))
            vrouter_name = vr_obj.get_fq_name()[-1]

        return vrouter_name

    def create_service(self, st, si):
        if not self.validate_network_config(st, si):
            return

        # get current vm list
        vm_list = [None] * si.max_instances
        for vm_id in si.virtual_machines:
            vm = VirtualMachineSM.get(vm_id)
            vm_list[vm.index] = vm

        # create and launch vm
        si.state = 'launching'
        instances = []
        for index in range(0, si.max_instances):
            vm = self._check_create_netns_vm(index, si, st, vm_list[index])
            if not vm:
                continue

            vr_name = self._associate_vrouter(si, vm)
            instances.append({'uuid': vm.uuid, 'vr_name': vr_name})

        # uve trace
        si.state = 'active'
        self.logger.uve_svc_instance((':').join(si.fq_name),
            status='CREATE', vms=instances,
            st_name=(':').join(st.fq_name))
