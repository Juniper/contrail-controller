# @author: Bartlomiej Biernacki

from .instance_manager import VRouterHostedManager
from vnc_api.vnc_api import *


class VRouterInstanceManager(VRouterHostedManager):
    """
    Manager for service instances (Docker or KVM) hosted on selected VRouter
    """
    def create_service(self, st_obj, si_obj):
        self.logger.log("Creating new VRouter instance!")
        si_props = si_obj.get_service_instance_properties()
        st_props = st_obj.get_service_template_properties()
        if st_props is None:
            self.logger.log("Cannot find service template associated to "
                            "service instance %s" % si_obj.get_fq_name_str())
            return

        # populate nic information
        nics = self._get_nic_info(si_obj, si_props, st_props)

        # this type can have only one instance
        instance_name = self._get_instance_name(si_obj, 0)
        try:
            vm_obj = self._vnc_lib.virtual_machine_read(
                fq_name=[instance_name])
            self.logger.log("Info: VM %s already exists" % instance_name)
        except NoIdError:
            vm_obj = VirtualMachine(instance_name)
            self._vnc_lib.virtual_machine_create(vm_obj)
            self.logger.log("Info: VM %s created" % instance_name)

        si_refs = vm_obj.get_service_instance_refs()
        if (si_refs is None) or (si_refs[0]['to'][0] == 'ERROR'):
            vm_obj.set_service_instance(si_obj)
            self._vnc_lib.virtual_machine_update(vm_obj)
            self.logger.log("Info: VM %s updated with SI %s" %
                            (instance_name, si_obj.get_fq_name_str()))

        # Create virtual machine interfaces with an IP on networks
        for nic in nics:
            vmi_obj = self._create_svc_vm_port(nic, instance_name,
                                               st_obj, si_obj)
            if vmi_obj.get_virtual_machine_refs() is None:
                vmi_obj.set_virtual_machine(vm_obj)
                self._vnc_lib.virtual_machine_interface_update(vmi_obj)
                self.logger.log("Info: VMI %s updated with VM %s" %
                                (vmi_obj.get_fq_name_str(), instance_name))

        vrouter_name = None
        state = 'pending'
        vrouter_back_refs = vm_obj.get_virtual_router_back_refs()
        vr_id = si_props.get_virtual_router_id()
        if (vrouter_back_refs is not None
                and vrouter_back_refs[0]['uuid'] != vr_id):
            # if it is not choosen vrouter remove machine from it
            vr_obj = self._vnc_lib.virtual_router_read(
                id=vr_id)
            if vr_obj:
                vr_obj.del_virtual_machine(vm_obj)
                self.logger.log("Info: VM %s removed from VRouter %s" %
                                (instance_name,
                                 ':'.join(vr_obj.get_fq_name())))
            vrouter_back_refs = None
        # Associate instance on the selected vrouter
        if vrouter_back_refs is None:
            vr_obj = self._vnc_lib.virtual_router_read(
                id=vr_id)
            if vr_obj:
                vr_obj.add_virtual_machine(vm_obj)
                chosen_vr_fq_name = vr_obj.get_fq_name()
                vrouter_name = chosen_vr_fq_name[-1]
                self._vnc_lib.virtual_router_update(vr_obj)
                state = 'active'
                self.logger.log("Info: VRouter %s updated with VM %s" %
                                (':'.join(chosen_vr_fq_name), instance_name))
        else:
            vrouter_name = vrouter_back_refs[0]['to'][-1]
            state = 'active'

        vm_db_entry = self._set_vm_db_info(1, instance_name,
                                           vm_obj.uuid, state, vrouter_name)
        self.db.service_instance_insert(si_obj.get_fq_name_str(),
                                        vm_db_entry)

        # uve trace
        self.logger.uve_svc_instance(si_obj.get_fq_name_str(),
                                     status='CREATE',
                                     vms=[{'uuid': vm_obj.uuid,
                                           'vr_name': vrouter_name}],
                                     st_name=st_obj.get_fq_name_str())
