#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.vnc_api import *
from kube_manager.vnc.config_db import *
import uuid

LOG = logging.getLogger(__name__)


class ServiceLbManager(object):

    def __init__(self, vnc_lib=None, logger=None):
        self._vnc_lib = vnc_lib
        self.logger = logger

    def read(self, id):
        try:
            lb_obj = self._vnc_lib.loadbalancer_read(id=id)
        except NoIdError:
            return None
        return lb_obj

    def delete(self, id):
        try:
            lb = self._vnc_lib.loadbalancer_read(id=id)
        except NoIdError:
            loadbalancerv2.EntityNotFound(name=self.resource_name, id=id)

        lb_vmi_refs = lb.get_virtual_machine_interface_refs()

        self._vnc_lib.loadbalancer_delete(id=id)
        self._delete_virtual_interface(lb_vmi_refs)

    def _get_instance_ip_back_refs(self, id):
        obj = self._vnc_lib.virtual_machine_interface_read(id = id, fields = ['instance_ip_back_refs'])
        back_refs = getattr(obj, 'instance_ip_back_refs', None)
        return back_refs

    def _create_virtual_interface(self, proj_obj, vn_obj, service_name,
                                  cluster_ip):

        vmi = VirtualMachineInterface(name=service_name, parent_obj=proj_obj)
        vmi.set_virtual_network(vn_obj)
        vmi.set_virtual_machine_interface_device_owner("K8S:LOADBALANCER")
        try:
            self._vnc_lib.virtual_machine_interface_create(vmi)
        except RefsExistError:
            self._vnc_lib.virtual_machine_interface_update(vmi)
        #VirtualMachineInterfaceKM.locate(vmi.uuid)
        iip_refs = self._get_instance_ip_back_refs(vmi.uuid)

        if iip_refs is not None:
            iip = self._vnc_lib.instance_ip_read(id=iip_refs[0]['uuid'])
            if iip.get_instance_ip_address() == cluster_ip:
                return vmi, cluster_ip

            fip_refs = iip.get_floating_ips()
            for ref in fip_refs or []:
                self._vnc_lib.floating_ip_delete(id=ref['uuid'])
            self._vnc_lib.instance_ip_delete(id=iip_refs[0]['uuid'])

        iip_obj = InstanceIp(name=service_name)
        iip_obj.set_virtual_network(vn_obj)
        iip_obj.set_virtual_machine_interface(vmi)
        if cluster_ip:
            iip_obj.set_instance_ip_address(cluster_ip)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        #InstanceIpKM.locate(iip_obj.uuid)
        iip = self._vnc_lib.instance_ip_read(id=iip_obj.uuid)
        vip_address = iip.get_instance_ip_address()

        return vmi, vip_address

    def _delete_virtual_interface(self, vmi_list):
        if vmi_list is None:
            return

        for vmi_ref in vmi_list:
            interface_id = vmi_ref['uuid']
            try:
                vmi = self._vnc_lib.virtual_machine_interface_read(id=interface_id)
            except NoIdError as ex:
                LOG.error(ex)
                continue

            ip_refs = vmi.get_instance_ip_back_refs()
            if ip_refs:
                for ip_ref in ip_refs:
                    try:
                        ip = self._vnc_lib.instance_ip_read(id=ip_ref['uuid'])
                    except NoIdError as ex:
                        LOG.error(ex)
                        continue

                    fip_refs = ip.get_floating_ips()
                    for ref in fip_refs or []:
                        self._vnc_lib.floating_ip_delete(id=ref['uuid'])

                    self._vnc_lib.instance_ip_delete(id=ip_ref['uuid'])

            self._vnc_lib.virtual_machine_interface_delete(id=interface_id)

    def _check_provider_exists(self, loadbalancer_provider):
        """
        Check if service-appliance-set for provider exists in the API
        """
        try:
            sas_fq_name = ["default-global-system-config"]
            sas_fq_name.append(loadbalancer_provider)
            sas_obj = self._vnc_lib.service_appliance_set_read(fq_name=sas_fq_name)
        except NoIdError:
            return None

        return sas_obj

    def create(self, vn_obj, service_id, service_name, proj_obj, cluster_ip, selectors):
        """
        Create a loadbalancer.
        """
        lb_obj = Loadbalancer(name=service_name, parent_obj=proj_obj,
                              loadbalancer_provider='native')
        lb_obj.uuid = service_id
        sas_obj = self._check_provider_exists(loadbalancer_provider='native')
        if sas_obj is not None:
            lb_obj.set_service_appliance_set(sas_obj)

        vmi_obj, vip_address = self._create_virtual_interface(proj_obj,
            vn_obj, service_name, cluster_ip)
        lb_obj.set_virtual_machine_interface(vmi_obj)

        props = LoadbalancerType(provisioning_status='ACTIVE', 
                      operating_status='ONLINE', vip_address=vip_address)
        lb_obj.set_loadbalancer_properties(props)

        try:
            self._vnc_lib.loadbalancer_create(lb_obj)
        except RefsExistError:
            self._vnc_lib.loadbalancer_update(lb_obj)
        return lb_obj

class ServiceLbListenerManager(object):

    def __init__(self, vnc_lib=None, logger=None):
        self._vnc_lib = vnc_lib
        self.logger = logger

    def read(self, id):
        return self._vnc_lib.loadbalancer_listener_read(id=id)

    def delete(self, id):
        return self._vnc_lib.loadbalancer_listener_delete(id=id)

    def create(self, lb_obj, proj_obj, port):

        obj_uuid = str(uuid.uuid1())
        name = lb_obj.name + "-" + port['protocol'] + "-" + str(port['port'])

        id_perms = IdPermsType(enable=True)
        ll_obj = LoadbalancerListener(name, proj_obj, id_perms=id_perms,
                                  display_name=name)
        ll_obj.uuid = obj_uuid

        if lb_obj:
            ll_obj.set_loadbalancer(lb_obj)

        props = LoadbalancerListenerType()
        if port and port['protocol']:
            if port['protocol'] == "TCP":
                props.set_protocol("TCP")
            else:
                props.set_protocol("UDP")

        if port and port['port']:
            props.set_protocol_port(port['port'])

        ll_obj.set_loadbalancer_listener_properties(props)
        ll_obj.add_annotations(KeyValuePair(key='targetPort', value=port['targetPort']))

        try:
            self._vnc_lib.loadbalancer_listener_create(ll_obj)
        except RefsExistError:
            self._vnc_lib.loadbalancer_listener_update(ll_obj)

        return ll_obj

class ServiceLbPoolManager(object):

    def __init__(self, vnc_lib=None, logger=None):
        self._vnc_lib = vnc_lib
        self.logger = logger

    def read(self, id):
        return self._vnc_lib.loadbalancer_pool_read(id=id)

    def delete(self, id):
        return self._vnc_lib.loadbalancer_pool_delete(id=id)

    def create(self, ll_obj, proj_obj, port):
        """
        Create a loadbalancer_pool object.
        """
        pool_uuid = str(uuid.uuid1())
        props = LoadbalancerPoolType()
        if port['protocol'] == "TCP":
            props.set_protocol("TCP")
        else:
            props.set_protocol("UDP")
        id_perms = IdPermsType(enable=True)
        pool_obj = LoadbalancerPool(ll_obj.name, proj_obj, uuid=pool_uuid,
                                loadbalancer_pool_properties=props,
                                id_perms=id_perms)

        if ll_obj:
            pool_exists = ll_obj.get_loadbalancer_pool_back_refs()
            if pool_exists is not None:
                raise loadbalancerv2.OnePoolPerListener(
                                     listener_id=p['listener_id'],
                                     pool_id=pool_exists[0]['uuid'])
            pool_obj.set_loadbalancer_listener(ll_obj)

        try:
            self._vnc_lib.loadbalancer_pool_create(pool_obj)
        except RefsExistError:
            self._vnc_lib.loadbalancer_pool_update(pool_obj)

        return pool_obj

class ServiceLbMemberManager(object):

    def __init__(self, vnc_lib=None, logger=None):
        self._vnc_lib = vnc_lib
        self.logger = logger

    def read(self, id):
        return self._vnc_lib.loadbalancer_member_read(id=id)

    def delete(self, id):
        return self._vnc_lib.loadbalancer_member_delete(id=id)

    def create(self, pool_obj, vmi_uuid, target_port):
        """
        Create a loadbalancer_member object.
        """
        obj_uuid = str(uuid.uuid1())
        props = LoadbalancerMemberType(protocol_port=target_port)
        id_perms = IdPermsType(enable=True)

        member_obj = LoadbalancerMember(
            obj_uuid, pool_obj, loadbalancer_member_properties=props,
            id_perms=id_perms)
        member_obj.uuid = obj_uuid

        member_obj.add_annotations(KeyValuePair(key='vmi', value=vmi_uuid))
        self._vnc_lib.loadbalancer_member_create(member_obj)
        return member_obj
