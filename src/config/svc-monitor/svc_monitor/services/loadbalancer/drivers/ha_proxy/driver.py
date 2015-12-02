#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import uuid

import svc_monitor.services.loadbalancer.drivers.abstract_driver as abstract_driver

from vnc_api.vnc_api import ServiceTemplate, ServiceInstance, ServiceInstanceType
from vnc_api.vnc_api import ServiceScaleOutType, ServiceInstanceInterfaceType
from vnc_api.vnc_api import NoIdError, RefsExistError

from svc_monitor.config_db import *

LOADBALANCER_SERVICE_TEMPLATE = [
    'default-domain',
    'haproxy-loadbalancer-template'
]


class OpencontrailLoadbalancerDriver(
        abstract_driver.ContrailLoadBalancerAbstractDriver):
    def __init__(self, name, manager, api, db, args=None):
        self._name = name
        self._api = api
        self._svc_manager = manager
        self._lb_template = None
        self.db = db

    def get_lb_template(self):
        st = ServiceTemplateSM.get(self._lb_template)
        template_obj = ServiceTemplate(st.name, parent_type = 'domain',
                                       fq_name = st.fq_name)
        template_obj.uuid = st.uuid
        return template_obj
    #end

    def _get_template(self):
        if self._lb_template is not None:
            return

        for st in ServiceTemplateSM.values():
            if st.fq_name == LOADBALANCER_SERVICE_TEMPLATE:
                self._lb_template = st.uuid
                return

    def _get_interface_address(self, vmi):
        for iip_id in  vmi.instance_ips:
            instance_ip = InstanceIpSM.get(iip_id)
            return instance_ip.address
        return None

    def _calculate_instance_properties(self, pool, vip):
        """ ServiceInstance settings
        - right network: public side, determined by the vip
        - left network: backend, determined by the pool subnet
        """
        props = ServiceInstanceType()
        if_list = []

        # Calculate the Right Interface from virtual ip property
        vmi = VirtualMachineInterfaceSM.get(vip.virtual_machine_interface)
        if not vmi:
            return None
        right_ip_address = self._get_interface_address(vmi)
        if right_ip_address is None:
            return None

        vip_vn = VirtualNetworkSM.get(vmi.virtual_network)
        if vip_vn is None:
            return None
        right_virtual_network = ':'.join(vip_vn.fq_name)

        right_if = ServiceInstanceInterfaceType(
            virtual_network=right_virtual_network,
            ip_address=right_ip_address)
        if_list.append(right_if)

        # Calculate the Left Interface from Pool property
        pool_attrs = pool.params
        pool_vn_id = self._api.kv_retrieve(pool_attrs['subnet_id']).split()[0]
        if pool_vn_id != vip_vn.uuid:
            pool_vn = VirtualNetworkSM.get(pool_vn_id)
            left_virtual_network = ':'.join(pool_vn.fq_name)
            left_if = ServiceInstanceInterfaceType(
                virtual_network=left_virtual_network)
            if_list.append(left_if)

        # set interfaces and ha
        props.set_interface_list(if_list)
        props.set_ha_mode('active-standby')
        scale_out = ServiceScaleOutType(max_instances=2, auto_scale=False)
        props.set_scale_out(scale_out)

        return props

    def _service_instance_update_props(self, si, nprops):
        old_ifs = si.params.get('interface_list', [])
        new_ifs = nprops.get_interface_list()
        update = False
        if len(new_ifs) != len(old_ifs):
            update = True
        else:
            for index, new_if in enumerate(new_ifs):
                if new_if.get_ip_address() != old_ifs[index]['ip_address']:
                    update = True
                    break
                if new_if.get_virtual_network() != \
                   old_ifs[index]['virtual_network']:
                    update = True
                    break
        if update:
            si_obj = ServiceInstance(name=si.name, parent_type='project')
            si_obj.uuid = si.uuid
            si_obj.set_service_instance_properties(nprops)
            self._api.service_instance_update(si_obj)
            si.update()

    def _update_loadbalancer_instance(self, pool_id, vip_id):
        """ Update the loadbalancer service instance.

        Prerequisites:
        pool and vip must be known.
        """
        pool = LoadbalancerPoolSM.get(pool_id)
        if pool is None:
            msg = ('Unable to retrieve pool %s' % pool_id)
            self._svc_manager.logger.log_error(msg)
            return

        vip = VirtualIpSM.get(vip_id)
        if vip is None:
            msg = ('Unable to retrieve virtual ip %s' % vip_id)
            self._svc_manager.logger.log_error(msg)
            return

        fq_name = pool.fq_name[:-1]
        fq_name.append(pool_id)

        si_refs = pool.service_instance
        si_obj = ServiceInstanceSM.get(si_refs)
        props = self._calculate_instance_properties(pool, vip)
        if props is None:
            try:
                self._api.service_instance_delete(id=si_refs)
                ServiceInstanceSM.delete(si_refs)
            except RefsExistError as ex:
                self._svc_manager.logger.log_error(str(ex))
            return

        if si_obj:
            self._service_instance_update_props(si_obj, props)
        else:
            si_obj = ServiceInstance(name=fq_name[-1], parent_type='project',
                fq_name=fq_name, service_instance_properties=props)
            si_obj.set_service_template(self.get_lb_template())
            self._api.service_instance_create(si_obj)
            ServiceInstanceSM.locate(si_obj.uuid)

        if si_refs is None or si_refs != si_obj.uuid:
            self._api.ref_update('loadbalancer-pool', pool.uuid,
                'service_instance_refs', si_obj.uuid, None, 'ADD')
        self.db.pool_driver_info_insert(pool_id,
                                        {'service_instance': si_obj.uuid})

    def _clear_loadbalancer_instance(self, tenant_id, pool_id):
        driver_data = self.db.pool_driver_info_get(pool_id)
        if driver_data is None:
            return
        si_id = driver_data['service_instance']
        si = ServiceInstanceSM.get(si_id)
        if si is None:
            return

        pool_id = si.loadbalancer_pool
        pool = LoadbalancerPoolSM.get(pool_id)
        if pool:
            self._api.ref_update('loadbalancer-pool', pool_id,
                  'service_instance_refs', si_id, None, 'DELETE')
        try:
            self._api.service_instance_delete(id=si_id)
            ServiceInstanceSM.delete(si_id)
        except RefsExistError as ex:
            self._svc_manager.logger.log_error(str(ex))
        self.db.pool_remove(pool_id, ['service_instance'])

    def create_vip(self, vip):
        """A real driver would invoke a call to his backend
        and set the Vip status to ACTIVE/ERROR according
        to the backend call result
        self.plugin.update_status(Vip, vip["id"],
                                  constants.ACTIVE)
        """
        self._get_template()
        if vip['pool_id']:
            self._update_loadbalancer_instance(vip['pool_id'], vip['id'])

    def update_vip(self, old_vip, vip):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Vip, id, constants.ACTIVE)
        """
        if old_vip['pool_id'] != vip['pool_id']:
            self._clear_loadbalancer_instance(
                old_vip['tenant_id'], old_vip['pool_id'])

        if vip['pool_id']:
            self._update_loadbalancer_instance(vip['pool_id'], vip['id'])

    def delete_vip(self, vip):
        """A real driver would invoke a call to his backend
        and try to delete the Vip.
        if the deletion was successful, delete the record from the database.
        if the deletion has failed, set the Vip status to ERROR.
        """
        if vip['pool_id']:
            self._clear_loadbalancer_instance(vip['tenant_id'], vip['pool_id'])

    def create_pool(self, pool):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Pool, pool["id"],
                                  constants.ACTIVE)
        """
        self._get_template()
        if pool.get('vip_id'):
            self._update_loadbalancer_instance(pool['id'], pool['vip_id'])

    def update_pool(self, old_pool, pool):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(
                                  Pool,
                                  pool["id"], constants.ACTIVE)
        """
        if pool['vip_id']:
            self._update_loadbalancer_instance(pool['id'], pool['vip_id'])

    def delete_pool(self, pool):
        """Driver can call the code below in order to delete the pool.
        self.plugin._delete_db_pool(pool["id"])
        or set the status to ERROR if deletion failed
        """
        if pool['vip_id']:
            self._clear_loadbalancer_instance(pool['tenant_id'], pool['id'])

    def stats(self, pool_id):
        pass

    def create_member(self, member):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Member, member["id"],
                                   constants.ACTIVE)
        """
        pass

    def update_member(self, old_member, member):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Member,
                                  member["id"], constants.ACTIVE)
        """
        pass

    def delete_member(self, member):
        pass

    def update_pool_health_monitor(self,
                                   old_health_monitor,
                                   health_monitor,
                                   pool_id):
        pass

    def create_pool_health_monitor(self,
                                   health_monitor,
                                   pool_id):
        """Driver may call the code below in order to update the status.
        self.plugin.update_pool_health_monitor(
                                               health_monitor["id"],
                                               pool_id,
                                               constants.ACTIVE)
        """
        pass

    def delete_pool_health_monitor(self, health_monitor, pool_id):
        pass

    def update_health_monitor(self, id, health_monitor):
        pass
