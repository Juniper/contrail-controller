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

    def delete(self, service_id):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return

        self._vnc_lib.loadbalancer_delete(id=service_id)
        vmi_ids = lb.virtual_machine_interfaces
        if vmi_ids is None:
            return None
        self._delete_virtual_interface(vmi_ids)


    def _create_virtual_interface(self, proj_obj, vn_obj, service_name,
                                  vip_address=None, subnet_uuid=None):
        obj_uuid = str(uuid.uuid1())
        name = obj_uuid + "-" + service_name
        vmi_obj = VirtualMachineInterface(name=name, parent_obj=proj_obj)
        vmi_obj.uuid = obj_uuid
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine_interface_device_owner("K8S:LOADBALANCER")
        sg_obj = SecurityGroup("default", proj_obj)
        vmi_obj.add_security_group(sg_obj)
        try:
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
            VirtualMachineInterfaceKM.locate(vmi_obj.uuid)
            iip_ids = set()
            vmi_id = vmi_obj.uuid
        except BadRequest as e:
            return None, None
        except RefsExistError:
            vmi_id = self._vnc_lib.fq_name_to_id('virtual-machine-interface',vmi_obj.get_fq_name())
            if vmi_id:
                vmi = VirtualMachineInterfaceKM.get(vmi_id)
                iip_ids = vmi.instance_ips
            else:
                self.logger.warning("Create virtual interface failed for"
                    " service (" + service_name + ")" + " RefExistError but vmi doesn't exist")
                return None, None

        try:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        except NoIdError:
            self.logger.warning("Read Service virtual interface failed for"
                " service (" + service_name + ")" + " NoIdError for vmi(" + vmi_id + ") doesn't exist")
            return None, None

        for iip_id in list(iip_ids):
            iip_obj = self._vnc_lib.instance_ip_read(id=iip_id)
            if iip_obj.get_instance_ip_address() == vip_address:
                return vmi, vip_address

            fip_refs = iip_obj.get_floating_ips()
            for ref in fip_refs or []:
                fip = self._vnc_lib.floating_ip_read(id=ref['uuid'])
                fip.set_virtual_machine_interface_list([])
                self._vnc_lib.floating_ip_update(fip)
                self._vnc_lib.floating_ip_delete(id=ref['uuid'])
            self._vnc_lib.instance_ip_delete(id=iip_obj.uuid)

        ip_name = str(uuid.uuid4())
        iip_obj = InstanceIp(name=ip_name)
        iip_obj.uuid = ip_name
        iip_obj.set_virtual_network(vn_obj)
        if subnet_uuid:
            iip_obj.set_subnet_uuid(subnet_uuid)
        iip_obj.set_virtual_machine_interface(vmi_obj)
        iip_obj.set_display_name(service_name)
        if vip_address:
            iip_obj.set_instance_ip_address(vip_address)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpKM.locate(iip_obj.uuid)
        iip_obj = self._vnc_lib.instance_ip_read(id=iip_obj.uuid)
        vip_address = iip_obj.get_instance_ip_address()

        return vmi_obj, vip_address

    def _delete_virtual_interface(self, vmi_ids):
        for vmi_id in vmi_ids:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if vmi:
                # Delete vmi-->floating-ip
                fip_ids = vmi.floating_ips.copy()
                for fip_id in fip_ids:
                    self._vnc_lib.floating_ip_delete(id=fip_id)

                ip_ids = vmi.instance_ips.copy()
                for ip_id in ip_ids:
                    ip = InstanceIpKM.get(ip_id)
                    if ip:
                        fip_ids = ip.floating_ips.copy()
                        for fip_id in fip_ids:
                            # Delete vmi-->instance-ip-->floating-ip
                            self._vnc_lib.floating_ip_delete(id=fip_id)

                        # Delete vmi-->instance-ip
                        self._vnc_lib.instance_ip_delete(id=ip_id)

                # Delete vmi
                self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)

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

    def create(self, lb_provider, vn_obj, service_id, service_name, proj_obj, vip_address=None, subnet_uuid=None):
        """
        Create a loadbalancer.
        """
        lb_obj = Loadbalancer(name=service_name, parent_obj=proj_obj,
                              loadbalancer_provider=lb_provider)
        lb_obj.uuid = service_id
        sas_obj = self._check_provider_exists(loadbalancer_provider=lb_provider)
        if sas_obj is not None:
            lb_obj.set_service_appliance_set(sas_obj)

        vmi_obj, vip_address = self._create_virtual_interface(proj_obj,
            vn_obj, service_name, vip_address, subnet_uuid)
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
        name = obj_uuid + "-" + lb_obj.name + "-" + port['protocol'] + "-" + str(port['port'])

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
            elif port['protocol'] == "HTTP":
                props.set_protocol("HTTP")
            elif port['protocol'] == "HTTPS":
                props.set_protocol("HTTPS")
            else:
                props.set_protocol("UDP")

        if port and port['port']:
            props.set_protocol_port(port['port'])

        ll_obj.set_loadbalancer_listener_properties(props)
        if 'targetPort' in port:
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

    def create(self, ll_obj, proj_obj, port, lb_algorithm=None, annotations=None):
        """
        Create a loadbalancer_pool object.
        """
        pool_uuid = str(uuid.uuid1())
        props = LoadbalancerPoolType()
        if port['protocol'] == "TCP":
            props.set_protocol("TCP")
        elif port['protocol'] == "HTTP":
            props.set_protocol("HTTP")
        elif port['protocol'] == "HTTPS":
            props.set_protocol("HTTPS")
        else:
            props.set_protocol("UDP")
        if lb_algorithm:
            props.set_loadbalancer_method(lb_algorithm)
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

        if annotations:
            for key in annotations:
                pool_obj.add_annotations(KeyValuePair(key=key, value=annotations[key]))
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

    def create(self, pool_obj, address, port, annotations):
        """
        Create a loadbalancer_member object.
        """
        obj_uuid = str(uuid.uuid1())
        props = LoadbalancerMemberType(address=address, protocol_port=port)
        id_perms = IdPermsType(enable=True)

        member_obj = LoadbalancerMember(
            obj_uuid, pool_obj, loadbalancer_member_properties=props,
            id_perms=id_perms)
        member_obj.uuid = obj_uuid

        if annotations:
            for key in annotations:
                member_obj.add_annotations(KeyValuePair(key=key, value=annotations[key]))
        self._vnc_lib.loadbalancer_member_create(member_obj)
        return member_obj

    def update(self, member_id, address, port, annotations):
        """
        Update a loadbalancer_member object.
        """
        member_obj = self._vnc_lib.loadbalancer_member_read(id=member_id)
        props = LoadbalancerMemberType(address=address, protocol_port=port)
        member_obj.set_loadbalancer_member_properties(props)
        if annotations:
            kvps = []
            for key in annotations.keys():
                kvp = {}
                kvp['key'] = key
                kvp['value'] = annotations[key]
                kvps.append(kvp)
            member_obj.set_annotations(KeyValuePairs(kvps))
        self._vnc_lib.loadbalancer_member_update(member_obj)
        return member_obj
