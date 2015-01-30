#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import uuid

from neutron.common import exceptions as n_exc
from neutron.openstack.common import log as logging
import svc_monitor.services.loadbalancer.drivers.abstract_driver as abstract_driver

from vnc_api.vnc_api import ServiceInstance, ServiceInstanceType
from vnc_api.vnc_api import ServiceScaleOutType, ServiceInstanceInterfaceType
from vnc_api.vnc_api import NoIdError, RefsExistError
import neutron_plugin_contrail.plugins.opencontrail.loadbalancer.utils
import neutron_plugin_contrail.plugins.opencontrail.loadbalancer.utils as utils

LOG = logging.getLogger(__name__)

LOADBALANCER_SERVICE_TEMPLATE = [
    'default-domain',
    'haproxy-loadbalancer-template'
]


class OpencontrailLoadbalancerDriver(
        abstract_driver.ContrailLoadBalancerAbstractDriver):
    def __init__(self, manager, api, args=None):
        self._api = api
        self._lb_template = None

    def _get_template(self):
        if self._lb_template is not None:
            return
        try:
            tmpl = self._api.service_template_read(
                fq_name=LOADBALANCER_SERVICE_TEMPLATE)
        except NoIdError:
            msg = ('Loadbalancer service-template not found when '
                   'attempting to create pool')
            raise n_exc.BadRequest(resource='pool', msg=msg)
        self._lb_template = tmpl

    def _get_virtual_ip_interface(self, vip):
        vmi_list = vip.get_virtual_machine_interface_refs()
        if vmi_list is None:
            return None
        try:
            vmi = self._api.virtual_machine_interface_read(
                id=vmi_list[0]['uuid'])
        except NoIdError as ex:
            LOG.error(ex)
            return None
        return vmi

    def _get_interface_address(self, vmi):
        ip_refs = vmi.get_instance_ip_back_refs()
        if ip_refs is None:
            return None

        try:
            iip = self._api.instance_ip_read(id=ip_refs[0]['uuid'])
        except NoIdError as ex:
            LOG.error(ex)
            return None
        return iip.get_instance_ip_address()

    def _calculate_instance_properties(self, pool, vip):
        """ ServiceInstance settings
        - right network: public side, determined by the vip
        - left network: backend, determined by the pool subnet
        """
        props = ServiceInstanceType()
        if_list = []

        vmi = self._get_virtual_ip_interface(vip)
        if not vmi:
            return None

        vnet_refs = vmi.get_virtual_network_refs()
        if vnet_refs is None:
            return None
        right_virtual_network = ':'.join(vnet_refs[0]['to'])

        right_ip_address = self._get_interface_address(vmi)
        if right_ip_address is None:
            return None
        right_if = ServiceInstanceInterfaceType(
            virtual_network=right_virtual_network,
            ip_address=right_ip_address)
        if_list.append(right_if)

        pool_attrs = pool.get_loadbalancer_pool_properties()
        backnet_id = utils.get_subnet_network_id(
            self._api, pool_attrs.subnet_id)
        if backnet_id != vnet_refs[0]['uuid']:
            try:
                vnet = self._api.virtual_network_read(id=backnet_id)
            except NoIdError as ex:
                LOG.error(ex)
                return None
            left_virtual_network = ':'.join(vnet.get_fq_name())
            left_if = ServiceInstanceInterfaceType(
                virtual_network=left_virtual_network)
            if_list.append(left_if)

        # set interfaces and ha
        props.set_interface_list(if_list)
        props.set_ha_mode('active-standby')
        scale_out = ServiceScaleOutType(max_instances=2, auto_scale=False)
        props.set_scale_out(scale_out)

        return props

    def _service_instance_update_props(self, si_obj, nprops):
        fields = [
            'right_virtual_network',
            'right_ip_address',
            'left_virtual_network'
        ]

        current = si_obj.get_service_instance_properties()
        update = False

        for field in fields:
            if getattr(current, field) != getattr(nprops, field):
                update = True
                break

        si_obj.set_service_instance_properties(nprops)
        return update

    def _update_loadbalancer_instance(self, pool_id, vip_id):
        """ Update the loadbalancer service instance.

        Prerequisites:
        pool and vip must be known.
        """
        try:
            pool = self._api.loadbalancer_pool_read(id=pool_id)
        except NoIdError:
            msg = ('Unable to retrieve pool %s' % pool_id)
            raise n_exc.BadRequest(resource='pool', msg=msg)

        try:
            vip = self._api.virtual_ip_read(id=vip_id)
        except NoIdError:
            msg = ('Unable to retrieve virtual-ip %s' % vip_id)
            raise n_exc.BadRequest(resource='vip', msg=msg)

        fq_name = pool.get_fq_name()[:-1]
        fq_name.append(pool_id)

        props = self._calculate_instance_properties(pool, vip)
        if props is None:
            try:
                self._api.service_instance_delete(fq_name=fq_name)
            except RefsExistError as ex:
                LOG.error(ex)
            return

        self._get_template()

        try:
            si_obj = self._api.service_instance_read(fq_name=fq_name)
            update = self._service_instance_update_props(si_obj, props)
            if update:
                self._api.service_instance_update(si_obj)

        except NoIdError:
            proj_obj = self._api.project_read(fq_name=fq_name[:-1])
            si_obj = ServiceInstance(name=fq_name[-1], parent_obj=proj_obj,
                                     service_instance_properties=props)
            si_obj.set_service_template(self._lb_template)
            self._api.service_instance_create(si_obj)

        si_refs = pool.get_service_instance_refs()
        if si_refs is None or si_refs[0]['uuid'] != si_obj.uuid:
            pool.set_service_instance(si_obj)
            self._api.loadbalancer_pool_update(pool)

    def _clear_loadbalancer_instance(self, tenant_id, pool_id):
        try:
            project = self._api.project_read(
                id=str(uuid.UUID(tenant_id)))
        except NoIdError as ex:
            LOG.error(ex)
            return
        fq_name = list(project.get_fq_name())
        fq_name.append(pool_id)

        try:
            si_obj = self._api.service_instance_read(fq_name=fq_name)
        except NoIdError as ex:
            LOG.error(ex)
            return

        pool_back_refs = si_obj.get_loadbalancer_pool_back_refs()
        for pool_back_ref in pool_back_refs or []:
            pool_obj = self._api.loadbalancer_pool_read(
                id=pool_back_ref['uuid'])
            pool_obj.del_service_instance(si_obj)
            self._api.loadbalancer_pool_update(pool_obj)

        try:
            self._api.service_instance_delete(fq_name=fq_name)
        except RefsExistError as ex:
            LOG.error(ex)

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
        pass

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
