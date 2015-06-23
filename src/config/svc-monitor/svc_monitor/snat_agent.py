from vnc_api.vnc_api import *

from cfgm_common import exceptions as vnc_exc
from config_db import VirtualNetworkSM, LogicalRouterSM, VirtualMachineInterfaceSM, ServiceInstanceSM, ServiceTemplateSM

SNAT_SERVICE_TEMPLATE_FQ_NAME = ['default-domain', 'netns-snat-template']

class SNATAgent(object):

    def __init__(self, svc_mon, vnc_lib):
        self._vnc_lib = vnc_lib
        self._svc_mon = svc_mon
    # end __init__

    def audit_snat_instances(self):
        for lr in LogicalRouterSM.values():
            self.update_snat_instance(lr)
        for si in ServiceInstanceSM.values():
            si_name = si.fq_name[-1]
            st_obj = ServiceTemplateSM.get(si.service_template)
            if st_obj.params['service_type'] != "source-nat":
                continue
            lr_uuid = si_name.split('_')[-1]
            lr = LogicalRouterSM.get(lr_uuid)
            if lr is None or lr.virtual_network is None:
                self.cleanup_snat_instance(lr_uuid, si.uuid)
    # end audit_snat_instances

    def update_snat_instance(self, router_obj):
        if router_obj.virtual_network:
            if router_obj.service_instance is None:
                self.add_snat_instance(router_obj)
        else:
            if router_obj.service_instance:
                self.delete_snat_instance(router_obj)
    # end update_snat_instance
   
    def add_snat_instance(self, router_obj):
        try:
            vnc_rtr_obj = self._vnc_lib.logical_router_read(id=router_obj.uuid)
        except vnc_exc.NoIdError:
            # msg="Unable to read logical router to set the default gateway")
            return

        try:
            project_obj = self._vnc_lib.project_read(id=router_obj.parent_uuid)
        except vnc_exc.NoIdError:
            # msg="Unable to read project to set the default gateway")
            return

        # Get netns SNAT service template
        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=SNAT_SERVICE_TEMPLATE_FQ_NAME)
        except vnc_exc.NoIdError:
            # msg="Unable to read template to set the default gateway")
            return

        # Get the service instance if it exists
        si_name = 'si_' + router_obj.uuid
        si_fq_name = project_obj.fq_name + [si_name]
        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
            si_uuid = si_obj.uuid
        except vnc_exc.NoIdError:
            si_obj = None

        # Get route table for default route it it exists
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.fq_name + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
            rt_uuid = rt_obj.uuid
        except vnc_exc.NoIdError:
            rt_obj = None

        # Set the service instance
        si_created = False
        if not si_obj:
            si_obj = ServiceInstance(si_name, parent_obj=project_obj)
            si_created = True
        si_prop_obj = ServiceInstanceType(
            scale_out=ServiceScaleOutType(max_instances=2,
                                          auto_scale=True),
            auto_policy=True)

        # set right interface in order of [right, left] to match template
        left_if = ServiceInstanceInterfaceType()
        virtual_network = router_obj.virtual_network
        vn_obj = VirtualNetworkSM.get(virtual_network)
        right_if = ServiceInstanceInterfaceType(
            virtual_network=':'.join(vn_obj.fq_name))
        si_prop_obj.set_interface_list([right_if, left_if])
        si_prop_obj.set_ha_mode('active-standby')

        si_obj.set_service_instance_properties(si_prop_obj)
        si_obj.set_service_template(st_obj)
        if si_created:
            si_uuid = self._vnc_lib.service_instance_create(si_obj)
        else:
            self._vnc_lib.service_instance_update(si_obj)

        # Set the route table
        route_obj = RouteType(prefix="0.0.0.0/0",
                              next_hop=si_obj.get_fq_name_str())
        rt_created = False
        if not rt_obj:
            rt_obj = RouteTable(name=rt_name, parent_obj=project_obj)
            rt_created = True
        rt_obj.set_routes(RouteTableType.factory([route_obj]))
        if rt_created:
            rt_uuid = self._vnc_lib.route_table_create(rt_obj)
        else:
            self._vnc_lib.route_table_update(rt_obj)

        # Associate route table to all private networks connected onto
        # that router
        for intf in router_obj.virtual_machine_interfaces or []:
            vmi_obj = VirtualMachineInterfaceSM.locate(intf)
            net_id = vmi_obj.virtual_network
            try:
                net_obj = self._vnc_lib.virtual_network_read(id=net_id)
            except vnc_exc.NoIdError:
                continue
            net_obj.set_route_table(rt_obj)
            self._vnc_lib.virtual_network_update(net_obj)

        # Add logical gateway virtual network
        vnc_rtr_obj.set_service_instance(si_obj)
        self._vnc_lib.logical_router_update(vnc_rtr_obj)
    # end add_snat_instance

    def delete_snat_instance(self, router_obj):
        try:
            vnc_rtr_obj = self._vnc_lib.logical_router_read(id=router_obj.uuid)
        except vnc_exc.NoIdError:
            vnc_rtr_obj = None

        try:
            project_obj = self._vnc_lib.project_read(id=router_obj.parent_uuid)
        except vnc_exc.NoIdError:
            # msg="Unable to read project to set the default gateway")
            return

        # Get the service instance if it exists
        si_name = 'si_' + router_obj.uuid
        si_fq_name = project_obj.get_fq_name() + [si_name]
        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
            si_uuid = si_obj.uuid
        except vnc_exc.NoIdError:
            si_obj = None

        # Get route table for default route it it exists
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.get_fq_name() + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
            rt_uuid = rt_obj.uuid
        except vnc_exc.NoIdError:
            rt_obj = None

        # Delete route table
        if rt_obj:
            # Disassociate route table to all private networks connected
            # onto that router
            for net_ref in rt_obj.get_virtual_network_back_refs() or []:
                try:
                    net_obj = self._vnc_lib.virtual_network_read(
                        id=net_ref['uuid'])
                except vnc_exc.NoIdError:
                    continue
                net_obj.del_route_table(rt_obj)
                self._vnc_lib.virtual_network_update(net_obj)
            self._vnc_lib.route_table_delete(id=rt_obj.uuid)

        if vnc_rtr_obj:
            # Clear logical gateway virtual network
            vnc_rtr_obj.set_service_instance_list([])
            try:
                self._vnc_lib.logical_router_update(vnc_rtr_obj)
            except vnc_exc.NoIdError:
                pass

        # Delete service instance
        if si_obj:
            self._vnc_lib.service_instance_delete(id=si_uuid)
    # end delete_snat_instance

    def cleanup_snat_instance(self, lr_id, si_id):
        try:
            vnc_rtr_obj = self._vnc_lib.logical_router_read(id=lr_id)
        except vnc_exc.NoIdError:
            vnc_rtr_obj = None

        # Get the service instance if it exists
        try:
            si_obj = self._vnc_lib.service_instance_read(id=si_id)
        except vnc_exc.NoIdError:
            return

        # Get route table for default route it it exists
        rt_name = 'rt_' + lr_id
        rt_fq_name = si_obj.get_fq_name()[0:2] + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
            rt_uuid = rt_obj.uuid
        except vnc_exc.NoIdError:
            rt_obj = None

        # Delete route table
        if rt_obj:
            # Disassociate route table to all private networks connected
            # onto that router
            for net_ref in rt_obj.get_virtual_network_back_refs() or []:
                try:
                    net_obj = self._vnc_lib.virtual_network_read(
                        id=net_ref['uuid'])
                except vnc_exc.NoIdError:
                    continue
                net_obj.del_route_table(rt_obj)
                self._vnc_lib.virtual_network_update(net_obj)
            self._vnc_lib.route_table_delete(id=rt_obj.uuid)
        # Delete service instance
        if si_obj:
            self._vnc_lib.service_instance_delete(id=si_id)
    # end cleanup_snat_instance


