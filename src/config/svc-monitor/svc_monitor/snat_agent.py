import copy
import uuid

from vnc_api.vnc_api import *

from agent import Agent
from cfgm_common import exceptions as vnc_exc
from cfgm_common import svc_info
from config_db import VirtualNetworkSM, LogicalRouterSM, \
    VirtualMachineInterfaceSM, ServiceInstanceSM, ServiceTemplateSM, \
    ProjectSM, DBBaseSM


SNAT_SERVICE_TEMPLATE_FQ_NAME = ['default-domain', 'netns-snat-template']


class SNATAgent(Agent):

    def audit_snat_instances(self):
        for lr in LogicalRouterSM.values():
            self.update_snat_instance(lr)
        for si in ServiceInstanceSM.values():
            si_name = si.fq_name[-1]
            st_obj = ServiceTemplateSM.get(si.service_template)
            if st_obj.params['service_type'] != "source-nat":
                continue
            lr_uuid = si.logical_router
            lr = LogicalRouterSM.get(lr_uuid)
            if lr is None or lr.virtual_network is None:
                self.cleanup_snat_instance(lr_uuid, si.uuid)
    # end audit_snat_instances

    def handle_service_type(self):
        return svc_info.get_snat_service_type()

    def pre_create_service_vm(self, instance_index, si, st, vm):
        for nic in si.vn_info:
            nic['user-visible'] = False
        return True

    def _create_snat_vn(self, si_obj, vn_name):
        snat_subnet = svc_info.get_snat_left_subnet()
        self._svc_mon.netns_manager.create_service_vn(
            vn_name, snat_subnet, None, si_obj.fq_name[:-1],
            user_visible=False)

    def _get_snat_vn(self, si_obj):
        vn_name = '%s_%s' % (svc_info.get_snat_left_vn_prefix(),
                             si_obj.name)
        vn_fq_name = si_obj.fq_name[:-1] + [vn_name]
        try:
            self._cassandra.fq_name_to_uuid('virtual-network', vn_fq_name)
        except NoIdError:
            self._create_snat_vn(si_obj, vn_name)

        return ':'.join(vn_fq_name)

    def update_snat_instance(self, router_obj):
        if (router_obj.virtual_network and
            router_obj.virtual_machine_interfaces):
            if router_obj.service_instance is None:
                self._add_snat_instance(router_obj)
        else:
            if router_obj.service_instance:
                self.delete_snat_instance(router_obj)

        router_obj.last_virtual_machine_interfaces = copy.copy(
            router_obj.virtual_machine_interfaces)
        return router_obj
    # end update_snat_instance

    def _get_route_table(self, router_obj):
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = router_obj.fq_name[:-1] + [rt_name]
        try:
            return DBBaseSM().read_vnc_obj(
                obj_type="route_table", fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            return

    def _del_route_table(self, net_uuid, rt_obj):
        try:
            self._vnc_lib.ref_update(
                  'virtual-network', net_uuid, 'route-table',
                  None, rt_obj.get_fq_name(), 'DELETE', None)
        except vnc_exc.NoIdError:
            return

    def _set_lr_route_table(self, lr_uuid, rt_obj):
        self._vnc_lib.ref_update(
               'logical-router', lr_uuid, 'route-table',
               None, rt_obj.get_fq_name(), 'ADD', None)

    def upgrade(self, router_obj):
        rt_obj = self._get_route_table(router_obj)
        if rt_obj and not rt_obj.get_logical_router_back_refs():
            # remove route table links from all networks connected to logical router
            for vn_ref in rt_obj.virtual_network_back_refs or []:
                net_uuid = vn_ref.get('uuid')
                self._del_route_table(net_uuid, rt_obj)
            # Associate route table to logical router
            self._set_lr_route_table(router_obj.uuid, rt_obj)
    #end

    def _add_snat_instance(self, router_obj):
        try:
            vnc_rtr_obj = self._vnc_lib.logical_router_read(id=router_obj.uuid)
        except vnc_exc.NoIdError:
            # msg="Unable to read logical router to set the default gateway")
            return

        # Get netns SNAT service template
        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=SNAT_SERVICE_TEMPLATE_FQ_NAME)
        except vnc_exc.NoIdError:
            # msg="Unable to read template to set the default gateway")
            return

        # Get the service instance if it exists
        si_obj = None
        si_uuid = router_obj.service_instance
        if si_uuid:
            try:
                si_obj = self._vnc_lib.service_instance_read(id=si_uuid)
            except vnc_exc.NoIdError:
                pass

        # Get route table for default route it it exists
        rt_obj = self._get_route_table(router_obj)

        project_fq_name = router_obj.fq_name[:-1]
        # Set the service instance
        si_created = False
        if not si_obj:
            si_name = 'snat_' + router_obj.uuid + '_' + str(uuid.uuid4())
            si_obj = ServiceInstance(si_name)
            si_obj.fq_name = project_fq_name + [si_name]
            si_created = True
        si_prop_obj = ServiceInstanceType(
            scale_out=ServiceScaleOutType(max_instances=2,
                                          auto_scale=True),
            auto_policy=False)

        # set right interface in order of [right, left] to match template
        vn_left_fq_name = self._get_snat_vn(si_obj)
        left_if = ServiceInstanceInterfaceType(virtual_network=vn_left_fq_name)
        virtual_network = router_obj.virtual_network
        vn_obj = VirtualNetworkSM.get(virtual_network)
        right_if = ServiceInstanceInterfaceType(
            virtual_network=':'.join(vn_obj.fq_name))
        si_prop_obj.set_interface_list([right_if, left_if])
        si_prop_obj.set_ha_mode('active-standby')

        si_obj.set_service_instance_properties(si_prop_obj)
        si_obj.set_service_template(st_obj)

        if si_created:
            self._vnc_lib.service_instance_create(si_obj)
        else:
            self._vnc_lib.service_instance_update(si_obj)

        # Set the route table
        route_obj = RouteType(prefix="0.0.0.0/0",
                              next_hop=si_obj.get_fq_name_str())
        rt_created = False
        if not rt_obj:
            rt_name = 'rt_' + router_obj.uuid
            rt_obj = RouteTable(name=rt_name)
            rt_obj.fq_name = project_fq_name + [rt_name]
            rt_created = True
        rt_obj.set_routes(RouteTableType.factory([route_obj]))
        if rt_created:
            self._vnc_lib.route_table_create(rt_obj)
        else:
            self._vnc_lib.route_table_update(rt_obj)

        # Associate route table to logical router
        vnc_rtr_obj.add_route_table(rt_obj)

        # Add logical gateway virtual network
        vnc_rtr_obj.set_service_instance(si_obj)
        self._vnc_lib.logical_router_update(vnc_rtr_obj)
    # end add_snat_instance

    def delete_snat_vn(self, si_obj):
        vn_name = '%s_%s' % (svc_info.get_snat_left_vn_prefix(),
                             si_obj.name)
        vn_fq_name = si_obj.fq_name[:-1] + [vn_name]
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            return

        vn = VirtualNetworkSM.get(vn_obj.uuid)
        if not vn:
            return

        for vmi_id in vn.virtual_machine_interfaces:
            try:
                self._vnc_lib.ref_update('virtual-machine-interface',
                    vmi_id, 'virtual-network', vn.uuid, None, 'DELETE')
            except NoIdError:
                pass

        for iip_id in vn.instance_ips:
            try:
                self._vnc_lib.instance_ip_delete(id=iip_id)
            except NoIdError:
                pass

        try:
            self._vnc_lib.virtual_network_delete(id=vn.uuid)
        except (RefsExistError, NoIdError):
            pass

    def delete_snat_instance(self, router_obj):
        try:
            vnc_rtr_obj = self._vnc_lib.logical_router_read(id=router_obj.uuid)
        except vnc_exc.NoIdError:
            vnc_rtr_obj = None

        # Get the service instance if it exists
        si_obj = None
        si_uuid = router_obj.service_instance
        if si_uuid:
            try:
                si_obj = self._vnc_lib.service_instance_read(id=si_uuid)
            except NoIdError:
                pass

        # Get route table for default route it it exists
        rt_obj = self._get_route_table(router_obj)

        if vnc_rtr_obj:
            vnc_rtr_obj.set_service_instance_list([])
            # Clear logical gateway route table
            if rt_obj:
                vnc_rtr_obj.del_route_table(rt_obj)
            try:
                self._vnc_lib.logical_router_update(vnc_rtr_obj)
            except vnc_exc.NoIdError:
                pass

        # Delete route table
        if rt_obj:
            self._vnc_lib.route_table_delete(id=rt_obj.uuid)

        # Delete service instance
        if not si_obj:
            return

        # Delete left network
        self.delete_snat_vn(si_obj)

        # Delete service instance
        self._vnc_lib.service_instance_delete(id=si_uuid)
    # end delete_snat_instance

    def cleanup_snat_instance(self, lr_id, si_id):
        # Get the service instance if it exists
        try:
            si_obj = self._vnc_lib.service_instance_read(id=si_id)
        except vnc_exc.NoIdError:
            return

        # Delete route table
        if lr_id:
            # Disassociate route table to all private networks connected
            # onto that router
            rt_name = 'rt_' + lr_id
            rt_fq_name = si_obj.get_fq_name()[0:2] + [rt_name]
            try:
                self._vnc_lib.ref_update(
                       'logical-router', lr_id, 'route-table',
                       None, rt_fq_name, 'DELETE', None)
            except vnc_exc.NoIdError:
                pass
            try:
                self._vnc_lib.route_table_delete(fq_name=rt_fq_name)
            except vnc_exc.NoIdError:
                pass

        # Delete service instance
        self._vnc_lib.service_instance_delete(id=si_id)
    # end cleanup_snat_instance
