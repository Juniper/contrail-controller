#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.vnc_api import *
from kube_manager.vnc.config_db import *
import uuid
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_common import VncCommon

LOG = logging.getLogger(__name__)

class ServiceLbManager(VncCommon):

    def __init__(self):
        super(ServiceLbManager,self).__init__('ServiceLoadBalancer')
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self.logger = vnc_kube_config.logger()

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

        vmi_ids = lb.virtual_machine_interfaces
        self._vnc_lib.loadbalancer_delete(id=service_id)
        if vmi_ids is None:
            return None
        self._delete_virtual_interface(vmi_ids)

    def _create_virtual_interface(self, proj_obj, vn_obj, service_ns,
            service_name, vip_address=None, subnet_uuid=None):
        vmi_uuid = str(uuid.uuid4())
        vmi_name = VncCommon.make_name(service_name, vmi_uuid)
        vmi_display_name = VncCommon.make_display_name(service_ns, service_name)
        #Check if VMI exists, if yes, delete it.
        vmi_obj = VirtualMachineInterface(name=vmi_name, parent_obj=proj_obj,
                    display_name=vmi_display_name)
        try:
            vmi_id = self._vnc_lib.fq_name_to_id('virtual-machine-interface',vmi_obj.get_fq_name())
            if vmi_id:
                self.logger.error("Duplicate LB Interface %s, delete it" %
                                    vmi_obj.get_fq_name())
                vmi = VirtualMachineInterfaceKM.get(vmi_id)
                iip_ids = vmi.instance_ips
                for iip_id in list(iip_ids):
                    iip_obj = self._vnc_lib.instance_ip_read(id=iip_id)

                    fip_refs = iip_obj.get_floating_ips()
                    for fip_ref in fip_refs or []:
                        fip = self._vnc_lib.floating_ip_read(id=fip_ref['uuid'])
                        fip.set_virtual_machine_interface_list([])
                        self._vnc_lib.floating_ip_update(fip)
                        self._vnc_lib.floating_ip_delete(id=fip_ref['uuid'])
                    self._vnc_lib.instance_ip_delete(id=iip_obj.uuid)
                self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        except NoIdError:
            pass

        #Create LB VMI
        vmi_obj.name = vmi_name
        vmi_obj.uuid = vmi_uuid
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine_interface_device_owner("K8S:LOADBALANCER")
        sg_name = "-".join([vnc_kube_config.cluster_name(),
            service_ns, 'default'])
        sg_obj = SecurityGroup(sg_name, proj_obj)
        vmi_obj.add_security_group(sg_obj)
        sg_name = "-".join([vnc_kube_config.cluster_name(), service_ns, "sg"])
        sg_obj = SecurityGroup(sg_name, proj_obj)
        vmi_obj.add_security_group(sg_obj)
        try:
            self.logger.debug("Create LB Interface %s " % vmi_obj.get_fq_name())
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
            VirtualMachineInterfaceKM.locate(vmi_obj.uuid)
        except BadRequest as e:
            self.logger.warning("LB (%s) Interface create failed %s " % (service_name, str(e)))
            return None, None

        try:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_obj.uuid)
        except NoIdError:
            self.logger.warning("Read Service VMI failed for"
                " service (" + service_name + ")" + " with NoIdError for vmi(" + vmi_id + ")")
            return None, None

        #Create InstanceIP <--- LB VMI
        iip_uuid = str(uuid.uuid4())
        iip_name = VncCommon.make_name(service_name, iip_uuid)
        iip_display_name = VncCommon.make_display_name(service_ns, service_name)
        iip_obj = InstanceIp(name=iip_name, display_name=iip_display_name)
        iip_obj.uuid = iip_uuid
        iip_obj.set_virtual_network(vn_obj)
        if subnet_uuid:
            iip_obj.set_subnet_uuid(subnet_uuid)
        iip_obj.set_virtual_machine_interface(vmi_obj)
        iip_obj.set_display_name(service_name)
        if vip_address:
            iip_obj.set_instance_ip_address(vip_address)
        try:
            self.logger.debug("Create LB VMI InstanceIp %s " % iip_obj.get_fq_name())
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpKM.locate(iip_obj.uuid)
        iip_obj = self._vnc_lib.instance_ip_read(id=iip_obj.uuid)
        vip_address = iip_obj.get_instance_ip_address()
        self.logger.debug("Created LB VMI InstanceIp %s with VIP %s" %
                          (iip_obj.get_fq_name(), vip_address))

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
                            try:
                                self._vnc_lib.floating_ip_delete(id=fip_id)
                            except NoIdError:
                                # deleted by svc-monitor
                                pass

                        # Delete vmi-->instance-ip
                        self._vnc_lib.instance_ip_delete(id=ip_id)

                # Delete vmi
                self.logger.debug("Deleting LB Interface %s" % vmi.name)
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

    def create(self, k8s_event_type, service_ns, service_id, service_name,
               proj_obj, vn_obj, vip_address=None, subnet_uuid=None):
        """
        Create a loadbalancer.
        """
        lb_name = VncCommon.make_name(service_name, service_id)
        lb_display_name = VncCommon.make_display_name(service_ns, service_name)
        if k8s_event_type == 'Service':
            lb_provider = 'native'
        elif k8s_event_type == 'Ingress':
            lb_provider = 'opencontrail'
        lb_obj = Loadbalancer(name=lb_name, parent_obj=proj_obj,
                    loadbalancer_provider=lb_provider,
                    display_name=lb_display_name)

        lb_obj.uuid = service_id
        sas_obj = self._check_provider_exists(loadbalancer_provider=lb_provider)
        if sas_obj is not None:
            lb_obj.set_service_appliance_set(sas_obj)

        vmi_obj, vip_address = self._create_virtual_interface(proj_obj,
            vn_obj, service_ns, service_name, vip_address, subnet_uuid)
        if vmi_obj is None:
            return None
        lb_obj.set_virtual_machine_interface(vmi_obj)

        id_perms = IdPermsType(enable=True)
        props = LoadbalancerType(provisioning_status='ACTIVE', id_perms=id_perms,
                      operating_status='ONLINE', vip_address=vip_address)
        lb_obj.set_loadbalancer_properties(props)

        self.add_annotations(lb_obj, LoadbalancerKM.kube_fq_name_key,
                      service_ns, service_name, k8s_event_type=k8s_event_type)
        try:
            self._vnc_lib.loadbalancer_create(lb_obj)
        except RefsExistError:
            self._vnc_lib.loadbalancer_update(lb_obj)
        return lb_obj

class ServiceLbListenerManager(VncCommon):

    def __init__(self):
        super(ServiceLbListenerManager,self).__init__('ServiceLbListener')
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self.logger = vnc_kube_config.logger()

    def read(self, id):
        return self._vnc_lib.loadbalancer_listener_read(id=id)

    def delete(self, id):
        return self._vnc_lib.loadbalancer_listener_delete(id=id)

    def create(self, lb_obj, proj_obj, port):

        ll_uuid = str(uuid.uuid4())
        name = lb_obj.name + "-" + port['protocol'] + "-" + str(port['port']) + "-" + ll_uuid

        id_perms = IdPermsType(enable=True)
        ll_obj = LoadbalancerListener(name, proj_obj, id_perms=id_perms,
                                  display_name=name)
        ll_obj.uuid = ll_uuid

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
            ll_obj.add_annotations(KeyValuePair(key='targetPort', value=str(port['targetPort'])))

        try:
            self._vnc_lib.loadbalancer_listener_create(ll_obj)
        except RefsExistError:
            self._vnc_lib.loadbalancer_listener_update(ll_obj)

        return ll_obj

class ServiceLbPoolManager(VncCommon):

    def __init__(self):
        super(ServiceLbPoolManager,self).__init__('ServiceLbPool')
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self.logger = vnc_kube_config.logger()

    def read(self, id):
        return self._vnc_lib.loadbalancer_pool_read(id=id)

    def delete(self, id):
        return self._vnc_lib.loadbalancer_pool_delete(id=id)

    def create(self, ll_obj, proj_obj, port, lb_algorithm=None, annotations=None):
        """
        Create a loadbalancer_pool object.
        """
        pool_uuid = str(uuid.uuid4())
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

class ServiceLbMemberManager(VncCommon):

    def __init__(self):
        super(ServiceLbMemberManager,self).__init__('ServiceLbMember')
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self.logger = vnc_kube_config.logger()

    def read(self, id):
        return self._vnc_lib.loadbalancer_member_read(id=id)

    def delete(self, id):
        return self._vnc_lib.loadbalancer_member_delete(id=id)

    def create(self, pool_obj, address, port, annotations):
        """
        Create a loadbalancer_member object.
        """
        lm_uuid = str(uuid.uuid4())
        props = LoadbalancerMemberType(address=address, protocol_port=port)
        id_perms = IdPermsType(enable=True)

        member_obj = LoadbalancerMember(
            lm_uuid, pool_obj, loadbalancer_member_properties=props,
            id_perms=id_perms)
        member_obj.uuid = lm_uuid

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
