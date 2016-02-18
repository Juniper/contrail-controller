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
            if nic['type'] == svc_info.get_left_if_str():
                nic['user-visible'] = False
        return True

    def _create_snat_vn(self, si_obj, vn_name):
        snat_subnet = svc_info.get_snat_left_subnet()
        self._svc_mon.netns_manager.create_service_vn(
            vn_name, snat_subnet, None, si_obj.fq_name[:-1],
            user_visible=False)

    def _get_snat_vn(self, project_obj, si_obj):
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
                self._update_snat_instance(router_obj)
        else:
            if router_obj.service_instance:
                self.delete_snat_instance(router_obj)

        router_obj.last_virtual_machine_interfaces = copy.copy(
            router_obj.virtual_machine_interfaces)
        return router_obj
    # end update_snat_instance

    def _diff_virtual_interfaces(self, router_obj):
        uuids = set([uuid for uuid in
                     router_obj.virtual_machine_interfaces])

        to_del = router_obj.last_virtual_machine_interfaces - uuids
        to_add = uuids - router_obj.last_virtual_machine_interfaces

        return to_del, to_add

    def _get_net_uuids(self, vmi_uuids):
         return [
             VirtualMachineInterfaceSM.get(uuid).virtual_network
             for uuid in vmi_uuids]

    def _virtual_network_read(self, net_uuid):
        return DBBaseSM().read_vnc_obj(obj_type="virtual_network", uuid=net_uuid)

    def _add_route_table(self, net_uuid, rt_obj):
        net_obj = self._virtual_network_read(net_uuid)
        if not net_obj:
            return
        net_obj.set_route_table(rt_obj)
        self._vnc_lib.virtual_network_update(net_obj)

    def _add_route_tables(self, net_uuids, rt_obj):
        for net_uuid in net_uuids:
            self._add_route_table(net_uuid, rt_obj)

    def _del_route_table(self, net_uuid, rt_obj):
        net_obj = self._virtual_network_read(net_uuid)
        if not net_obj:
            return
        net_obj.del_route_table(rt_obj)
        self._vnc_lib.virtual_network_update(net_obj)

    def _del_route_tables(self, net_uuids, rt_obj):
        for net_uuid in net_uuids:
            self._del_route_table(net_uuid, rt_obj)

    def _update_snat_instance(self, router_obj):
        to_del, to_add = self._diff_virtual_interfaces(router_obj)
        if to_del or to_add:
            project_obj = ProjectSM.get(router_obj.parent_uuid)

            rt_obj = self._get_route_table(router_obj, project_obj)
            if not rt_obj:
                return

            if to_add:
                net_uuids = self._get_net_uuids(to_add)
                self._add_route_tables(net_uuids, rt_obj)

            if to_del:
                net_uuids = self._get_net_uuids(to_del)
                self._del_route_tables(net_uuids, rt_obj)

    def _get_route_table(self, router_obj, project_obj):
        rt_name = 'rt_' + router_obj.uuid
        rt_fq_name = project_obj.fq_name + [rt_name]
        try:
            return DBBaseSM().read_vnc_obj(
                obj_type="route_table", fq_name=rt_fq_name)
        except vnc_exc.NoIdError:
            return

    def _add_snat_instance(self, router_obj):
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
        si_obj = None
        si_uuid = router_obj.service_instance
        if si_uuid:
            try:
                si_obj = self._vnc_lib.service_instance_read(id=si_uuid)
            except vnc_exc.NoIdError:
                pass

        # Get route table for default route it it exists
        rt_obj = self._get_route_table(router_obj, project_obj)

        # Set the service instance
        si_created = False
        if not si_obj:
            si_name = 'snat_' + router_obj.uuid + '_' + str(uuid.uuid4())
            si_obj = ServiceInstance(si_name, parent_obj=project_obj)
            si_created = True
        si_prop_obj = ServiceInstanceType(
            scale_out=ServiceScaleOutType(max_instances=2,
                                          auto_scale=True),
            auto_policy=True)

        # set right interface in order of [right, left] to match template
        vn_left_fq_name = self._get_snat_vn(project_obj, si_obj)
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
            rt_obj = RouteTable(name=rt_name, parent_obj=project_obj)
            rt_created = True
        rt_obj.set_routes(RouteTableType.factory([route_obj]))
        if rt_created:
            self._vnc_lib.route_table_create(rt_obj)
        else:
            self._vnc_lib.route_table_update(rt_obj)

        # Associate route table to all private networks connected onto
        # that router
        net_uuids = self._get_net_uuids(router_obj.virtual_machine_interfaces)
        self._add_route_tables(net_uuids, rt_obj)

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
        si_obj = None
        si_uuid = router_obj.service_instance
        if si_uuid:
            try:
                si_obj = self._vnc_lib.service_instance_read(id=si_uuid)
            except NoIdError:
                pass

        # Get route table for default route it it exists
        rt_obj = self._get_route_table(router_obj, project_obj)

        # Delete route table
        if rt_obj:
            if (hasattr(rt_obj, 'virtual_network_back_refs') and
                rt_obj.virtual_network_back_refs):
                # Disassociate route table to all private networks connected
                # onto that router
                uuids = [ref['uuid'] for ref in
                    rt_obj.virtual_network_back_refs]
                self._del_route_tables(uuids, rt_obj)
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
        # Get the service instance if it exists
        try:
            si_obj = self._vnc_lib.service_instance_read(id=si_id)
        except vnc_exc.NoIdError:
            return

        # Get route table for default route it it exists
        rt_name = 'rt_' + lr_id
        rt_fq_name = si_obj.get_fq_name()[0:2] + [rt_name]
        try:
            rt_obj = self._vnc_lib.route_table_read(
                         fq_name=rt_fq_name,
                         fields=['virtual_network_back_refs'])
        except vnc_exc.NoIdError:
            rt_obj = None

        # Delete route table
        if rt_obj:
            # Disassociate route table to all private networks connected
            # onto that router
            vn_back_refs = rt_obj.get_virtual_network_back_refs()
            if vn_back_refs:
                uuids = [ref['uuid'] for ref in vn_back_refs]
                self._del_route_tables(uuids, rt_obj)
            self._vnc_lib.route_table_delete(id=rt_obj.uuid)

        # Delete service instance
        self._vnc_lib.service_instance_delete(id=si_id)
    # end cleanup_snat_instance
