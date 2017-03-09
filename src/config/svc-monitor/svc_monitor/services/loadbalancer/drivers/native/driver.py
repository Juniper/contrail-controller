#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import uuid
import svc_monitor.services.loadbalancer.drivers.abstract_driver as abstract_driver

from vnc_api.vnc_api import InstanceIp
from vnc_api.vnc_api import FloatingIp
from vnc_api.vnc_api import PortMap, PortMappings
from vnc_api.vnc_api import ServiceHealthCheck, ServiceHealthCheckType

from vnc_api.vnc_api import NoIdError, RefsExistError

from svc_monitor.config_db import *

class OpencontrailLoadbalancerDriver(
        abstract_driver.ContrailLoadBalancerAbstractDriver):
    def __init__(self, name, manager, api, db, args=None):
        self._name = name
        self._api = api
        self._svc_manager = manager
        self.db = db

    def _get_floating_ip(self, iip_id=None, fip_id=None):
        fip = None
        try:
            if iip_id:
                iip = self._api.instance_ip_read(id=iip_id)
                fip_list = iip.get_floating_ips()
                if fip_list or []:
                    fip_id = fip_list[0]['uuid']
                else:
                    return None
            fip = self._api.floating_ip_read(id=fip_id)
        except NoIdError:
            fip = None
        return fip

    def _add_vmi_ref(self, vmi, iip_id=None, fip_id=None, fip=None):
        if not fip:
            fip = self._get_floating_ip(iip_id, fip_id)
        if fip:
            fip.add_virtual_machine_interface(vmi)
            self._api.floating_ip_update(fip)
        return fip

    def _delete_vmi_ref(self, vmi, iip_id=None, fip_id=None):
        fip = self._get_floating_ip(iip_id, fip_id)
        if fip:
            fip.del_virtual_machine_interface(vmi)
            self._api.floating_ip_update(fip)
        return fip

    def _add_port_map(self, fip, protocol, src_port, dst_port):
        portmap_entry = False
        portmappings = fip.get_floating_ip_port_mappings()
        portmap_list = []
        if portmappings:
            portmap_list = portmappings.get_port_mappings()
        if portmappings is None:
            portmappings = PortMappings()
            portmap_list = portmappings.get_port_mappings()
        for portmap in portmap_list or []:
            if portmap.src_port == src_port and portmap.protocol == protocol:
                portmap_entry = True
                break
        if portmap_entry == False:
            portmap = PortMap()
            portmap.set_protocol(protocol)
            portmap.set_src_port(src_port)
            portmap.set_dst_port(dst_port)
            portmappings.add_port_mappings(portmap)
            fip.set_floating_ip_port_mappings(portmappings)
            fip.floating_ip_port_mappings_enable = True
            self._api.floating_ip_update(fip)

    def _delete_port_map(self, fip, src_port):
        portmappings = fip.get_floating_ip_port_mappings()
        if not portmappings:
            return None
        portmap_list = portmappings.get_port_mappings()
        for portmap in portmap_list or []:
            if portmap.src_port == src_port:
                portmappings.delete_port_mappings(portmap)
                fip.set_floating_ip_port_mappings(portmappings)
                if len(portmap_list) == 0:
                    fip.floating_ip_port_mappings_enable = False
                self._api.floating_ip_update(fip)
                return portmap

    def _add_service_health_check_ref(self, service_health_check, vmi_id):
        vmi = self._api.virtual_machine_interface_read(id=vmi_id)
        if vmi:
            vmi.add_service_health_check(service_health_check)
            self._api.virtual_machine_interface_update(vmi)
        return vmi

    def _delete_service_health_check_ref(self, service_health_check, vmi_id):
        vmi = self._api.virtual_machine_interface_read(id=vmi_id)
        if vmi:
            vmi.del_service_health_check(service_health_check)
            self._api.virtual_machine_interface_update(vmi)
        return vmi

    def set_config_v2(self, lb_id):
        lb = LoadbalancerSM.get(lb_id)
        if not lb:
            return

        conf = {}
        vmi_conf = {}
        instance_ips = []
        floating_ips = []

        vmi = VirtualMachineInterfaceSM.get(lb.virtual_machine_interface)
        if vmi is None:
            return conf

        for iip_id in vmi.instance_ips or []:
            instance_ips.append(iip_id)
        for fip_id in vmi.floating_ips or []:
            floating_ips.append(fip_id)

        vmi_conf['instance_ips'] = instance_ips
        vmi_conf['floating_ips'] = floating_ips

        conf['vmi'] = vmi_conf

        return conf

    def _update_pool_ip_list(self, pool, action, iip_id=None, fip_id=None, fip=None):
        if action == "add":
            if iip_id:
                ip_id = iip_id
                pool.lb_instance_ips.append(iip_id)
            elif fip_id:
                ip_id = fip_id
                pool.lb_floating_ips.append(fip_id)
            if fip:
                pool.lb_fips[ip_id] = fip
        elif action == 'del':
            if iip_id and iip_id in pool.lb_instance_ips:
                ip_id = iip_id
                idx = pool.lb_instance_ips.index(ip_id)
                del pool.lb_instance_ips[idx]
            elif fip_id and fip_id in pool.lb_floating_ips:
                ip_id = fip_id
                idx = pool.lb_floating_ips.index(ip_id)
                del pool.lb_floating_ips[idx]
            if ip_id in pool.lb_fips.keys():
                del pool.lb_fips[ip_id]

    def _update_pool_member_props(self, lb, lb_props):
        if lb is None:
            return
        for ll_id in lb.loadbalancer_listeners:
            listener = LoadbalancerListenerSM.get(ll_id)
            if not listener:
                continue
            if listener.params['protocol'] == 'UDP':
                protocol = 'UDP'
            else:
                protocol = 'TCP'
            src_port = listener.params['protocol_port']
            pool = LoadbalancerPoolSM.get(listener.loadbalancer_pool)
            if pool:
                for iip_id in lb_props['old_instance_ips'] or []:
                    self._update_pool_ip_list(pool, "del", iip_id=iip_id)
                for fip_id in lb_props['old_floating_ips'] or []:
                    self._update_pool_ip_list(pool, "del", fip_id=fip_id)
                for iip_id in lb_props['new_instance_ips'] or []:
                    fip = self._get_floating_ip(iip_id=iip_id)
                    for member_id in pool.member_vmis.keys():
                        vmi = pool.member_vmis[member_id]
                        member = LoadbalancerMemberSM.get(member_id)
                        if not member:
                            continue
                        dst_port = member.params['protocol_port']
                        fip = self._add_vmi_ref(vmi, iip_id=iip_id, fip=fip)
                        self._add_port_map(fip, protocol, src_port, dst_port)
                    self._update_pool_ip_list(pool, "add", iip_id=iip_id, fip=fip)
                for fip_id in lb_props['new_floating_ips'] or []:
                    fip = self._get_floating_ip(fip_id=fip_id)
                    for member_id in pool.member_vmis.keys():
                        vmi = pool.member_vmis[member_id]
                        member = LoadbalancerMemberSM.get(member_id)
                        if not member:
                            continue
                        dst_port = member.params['protocol_port']
                        fip = self._add_vmi_ref(vmi, fip_id=fip_id, fip=fip)
                        self._add_port_map(fip, protocol, src_port, dst_port)
                    self._update_pool_ip_list(pool, "add", fip_id=fip_id, fip=fip)

    def _update_loadbalancer_props(self, lb_id):
        lb = LoadbalancerSM.get(lb_id)
        if lb is None:
            msg = ('Unable to retrieve loadbalancer %s' % lb_id)
            self._svc_manager.logger.error(msg)
            return

        driver_data = self.db.loadbalancer_driver_info_get(lb_id)
        if driver_data:
            if 'lb_instance_ips' in driver_data:
                lb.instance_ips = driver_data['lb_instance_ips']
            if 'lb_floating_ips' in driver_data:
                lb.floating_ips = driver_data['lb_floating_ips']

        vmi = VirtualMachineInterfaceSM.get(lb.virtual_machine_interface)
        if vmi is None:
            return

        if set(lb.instance_ips) == vmi.instance_ips and \
                set(lb.floating_ips) == vmi.floating_ips:
            return

        old_instance_ips = []
        new_instance_ips = []
        if set(lb.instance_ips) != vmi.instance_ips:
            for iip_id in lb.instance_ips or []:
                if iip_id not in vmi.instance_ips:
                    old_instance_ips.append(iip_id)

            for iip_id in vmi.instance_ips or []:
                if iip_id not in lb.instance_ips:
                    new_instance_ips.append(iip_id)

        old_floating_ips = []
        new_floating_ips = []
        if set(lb.floating_ips) != vmi.floating_ips:
            for fip_id in lb.floating_ips or []:
                if fip_id not in vmi.floating_ips:
                    old_floating_ips.append(fip_id)

            for fip_id in vmi.floating_ips or []:
                if fip_id not in lb.floating_ips:
                    new_floating_ips.append(fip_id)

        for iip_id in old_instance_ips or []:
            fip = self._get_floating_ip(vmi, iip_id=iip_id)
            if fip:
                fip.set_virtual_machine_interface_list([])
                self._api.floating_ip_update(fip)
                self._api.floating_ip_delete(id=fip.uuid)

        for fip_id in old_floating_ips or []:
            fip = self._get_floating_ip(fip_id=fip_id)
            if fip:
                fip.set_virtual_machine_interface_list([])
                fip.set_floating_ip_port_mappings([])
                fip.floating_ip_port_mappings_enable = False
                fip.floating_ip_traffic_direction = "both"
                self._api.floating_ip_update(fip)

        if len(new_instance_ips):
            for iip_id in new_instance_ips:
                iip = self._api.instance_ip_read(id=iip_id)
                fq_name = str(uuid.uuid4())
                fip = FloatingIp(name=fq_name, parent_obj=iip,
                             floating_ip_address=iip.instance_ip_address)
                fip.uuid = fq_name
                fip.floating_ip_traffic_direction = "ingress"
                self._api.floating_ip_create(fip)

        if len(new_floating_ips):
            for fip_id in new_floating_ips:
                fip = self._get_floating_ip(fip_id=fip_id)
                fip.floating_ip_traffic_direction = "ingress"
                self._api.floating_ip_update(fip)

        lb_props = {}
        lb_props['old_instance_ips'] = old_instance_ips
        lb_props['old_floating_ips'] = old_floating_ips
        lb_props['new_instance_ips'] = new_instance_ips
        lb_props['new_floating_ips'] = new_floating_ips

        self._update_pool_member_props(lb, lb_props)

        lb.instance_ips = vmi.instance_ips
        lb.floating_ips = vmi.floating_ips

        driver_data = {}
        driver_data['vmi'] = vmi.uuid
        driver_data['lb_instance_ips'] = list(lb.instance_ips)
        driver_data['lb_floating_ips'] = list(lb.floating_ips)

        self.db.loadbalancer_driver_info_insert(lb_id, driver_data)

    def _clear_loadbalancer_props(self, lb_id):
        driver_data = self.db.loadbalancer_driver_info_get(lb_id)
        if driver_data is None:
            return

        lb = LoadbalancerSM.get(lb_id)
        if lb is None:
            return

        lb.instance_ips = driver_data['lb_instance_ips']
        for iip_id in lb.instance_ips or []:
            fip = self._get_floating_ip(iip_id=iip_id)
            if fip:
                try:
                    fip.set_virtual_machine_interface_list([])
                    self._api.floating_ip_update(fip)
                    self._api.floating_ip_delete(id=fip.uuid)
                except NoIdError:
                    # probably deleted by the lb creator
                    pass

        try:
            lb.floating_ips = driver_data['lb_floating_ips']
            vmi = self._api.virtual_machine_interface_read(id=driver_data['vmi'])
            for fip_id in lb.floating_ips or []:
                fip = self._get_floating_ip(fip_id=fip_id)
                if fip:
                    fip.set_virtual_machine_interface_list([])
                    fip.set_floating_ip_port_mappings([])
                    fip.floating_ip_port_mappings_enable = False
                    fip.floating_ip_traffic_direction = "both"
                    self._api.floating_ip_update(fip)
                    fip = self._add_vmi_ref(vmi, fip_id=fip_id)
        except NoIdError:
            # probably deleted by the lb creator
            pass

        del lb.instance_ips[:]
        del lb.floating_ips[:]

        self.db.loadbalancer_remove(lb_id, ['vmi'])
        self.db.loadbalancer_remove(lb_id, ['lb_instance_ips'])
        self.db.loadbalancer_remove(lb_id, ['lb_floating_ips'])

    def _update_listener_props(self, old_listener, listener):
        lb_id = listener['loadbalancer_id']
        driver_data = self.db.loadbalancer_driver_info_get(lb_id)
        if driver_data is None:
            return
        lb_instance_ips = []
        lb_floating_ips = []
        if 'lb_instance_ips' in driver_data:
            lb_instance_ips = driver_data['lb_instance_ips']
        if 'lb_floating_ips' in driver_data:
            lb_floating_ips = driver_data['lb_floating_ips']

        if not old_listener:
            return

        if old_listener.props['protocol_port'] == listener.props['protocol_port'] and \
           old_listener.props['protocol'] == listener.props['protocol']:
            return

        if listener.params['protocol'] == 'UDP':
            protocol = 'UDP'
        else:
            protocol = 'TCP'

        for iip_id in lb_instance_ips or []:
            fip = self._get_floating_ip(iip_id=iip_id)
            if fip:
                src_port = old_listener.props['protocol_port']
                portmap = self._delete_port_map(fip, src_port)
                if portmap == None:
                    continue
                src_port = listener['protocol_port']
                dst_port = portmap.dst_port
                self._add_port_map(fip, protocol, src_port, dst_port)
        for fip_id in lb_floating_ips or []:
            fip = self._get_floating_ip(fip_id=fip_id)
            if fip:
                src_port = old_listener.props['protocol_port']
                portmap = self._delete_port_map(fip, src_port)
                if portmap == None:
                    continue
                src_port = listener['protocol_port']
                dst_port = portmap.dst_port
                self._add_port_map(fip, protocol, src_port, dst_port)

    def _clear_listener_props(self, listener_id):
        listener = LoadbalancerListenerSM.get(listener_id)
        if listener is None:
            return
        lb_id = listener['loadbalancer_id']
        driver_data = self.db.loadbalancer_driver_info_get(lb_id)
        if driver_data is None:
            return
        lb_instance_ips = []
        lb_floating_ips = []
        if 'lb_instance_ips' in driver_data:
            lb_instance_ips = driver_data['lb_instance_ips']
        if 'lb_floating_ips' in driver_data:
            lb_floating_ips = driver_data['lb_floating_ips']

        for iip_id in lb_instance_ips or []:
            fip = self._get_floating_ip(iip_id=iip_id)
            self._delete_port_map(fip, listener['protocol_port'])
        for iip_id in lb_floating_ips or []:
            fip = self._get_floating_ip(iip_id=iip_id)
            self._delete_port_map(fip, listener['protocol_port'])

    def _update_pool_props(self, pool_id):
        pool = LoadbalancerPoolSM.get(pool_id)
        if pool is None:
            return
        lb_id = pool.loadbalancer_id
        driver_data = self.db.loadbalancer_driver_info_get(lb_id)
        if driver_data is None:
            return
        lb_instance_ips = []
        lb_floating_ips = []
        if 'lb_instance_ips' in driver_data:
            lb_instance_ips = driver_data['lb_instance_ips']
        if 'lb_floating_ips' in driver_data:
            lb_floating_ips = driver_data['lb_floating_ips']

        if lb_instance_ips == pool.lb_instance_ips and \
            lb_floating_ips == pool.lb_floating_ips:
            return

        pool.lb_fips = {}
        pool.lb_instance_ips = []
        pool.lb_floating_ips = []
        for iip_id in lb_instance_ips or []:
            fip = self._get_floating_ip(iip_id=iip_id)
            self._update_pool_ip_list(pool, "add", iip_id=iip_id, fip=fip)
        for fip_id in lb_floating_ips or []:
            fip = self._get_floating_ip(fip_id=fip_id)
            self._update_pool_ip_list(pool, "add", fip_id=fip_id, fip=fip)

    def _clear_pool_props(self, pool_id):
        pool = LoadbalancerPoolSM.get(pool_id)
        if pool is None:
            return
        pool.lb_fips = {}
        pool.lb_instance_ips = []
        pool.lb_floating_ips = []
        pool.member_vmis = {}

    def _update_member_props(self, member_id):
        member = LoadbalancerMemberSM.get(member_id)
        if member is None or member.vmi is None:
            return

        pool = LoadbalancerPoolSM.get(member.loadbalancer_pool)
        if pool is None:
            return

        if member_id in pool.member_vmis.keys():
            vmi = pool.member_vmis[member_id]
        else:
            try:
                vmi = self._api.virtual_machine_interface_read(id=member.vmi)
                pool.member_vmis[member_id] = vmi
            except NoIdError:
                return

        protocol = pool.listener_protocol
        src_port = pool.listener_port
        dst_port = member.params['protocol_port']
        for key in pool.lb_fips.keys():
            fip = pool.lb_fips[key]
            fip.add_virtual_machine_interface(vmi)
            self._api.floating_ip_update(fip)
            self._add_port_map(fip, protocol, src_port, dst_port)
        if pool.service_health_check:
            self._add_service_health_check_ref(pool.service_health_check, member.vmi)
            member.service_health_check = service_health_check

    def _clear_member_props(self, member_id):
        member = LoadbalancerMemberSM.get(member_id)
        if member is None:
            return

        pool = LoadbalancerPoolSM.get(member.loadbalancer_pool)
        if pool is None:
            return

        if member_id in pool.member_vmis.keys():
            vmi = pool.member_vmis[member_id]
            del pool.member_vmis[member_id]
        else:
            try:
                vmi = self._api.virtual_machine_interface_read(id=member.vmi)
                pool.member_vmis[member_id] = vmi
            except NoIdError:
                return

        port_map_delete = False
        if len(pool.members) == 1 and list(pool.members)[0] == member_id:
            port_map_delete = True

        for key in pool.lb_fips.keys():
            fip = pool.lb_fips[key]
            fip.del_virtual_machine_interface(vmi)
            self._api.floating_ip_update(fip)
            if port_map_delete == False:
                continue
            self._delete_port_map(fip, pool.listener_port)

        if member.service_health_check:
            self._delete_service_health_check_ref(member.service_health_check, member.vmi)

    def _update_health_monitor_props(self, hm_id, pool_id):
        _service_health_check_type_mapping = {
            'delay': 'delay',
            'timeout': 'timeout',
            'max_retries': 'max_retries',
            'http_method': 'http_method',
            'url_path': 'url_path',
            'expected_codes': 'expected_codes',
            'monitor_type': 'monitor_type',
        }
        pool = LoadbalancerPoolSM.get(pool_id)
        if pool is None:
            return

        hm = HealthMonitorSM.get(hm_id)
        if hm is None:
            return

        props = ServiceHealthCheckType()
        setattr(props, 'health_check_type', 'link-local')

        for key, mapping in _service_health_check_type_mapping.iteritems():
            if mapping in hm.params:
                setattr(props, key, hm.params[mapping])

        if hm.params['monitor_type'] == 'PING' or \
               hm.params['monitor_type'] == 'TCP':
            setattr(props, 'monitor_type', 'PING')
            setattr(props, 'url_path', 'local-ip')
        elif hm.params['monitor_type'] == 'HTTP' or \
               hm.params['monitor_type'] == 'HTTPS':
            setattr(props, 'monitor_type', 'HTTP')

        props.enabled = True

        try:
            if hm.service_health_check_id:
                id = hm.service_health_check_id
                service_health_check = self._api.service_health_check_read(id=id)
                service_health_check.set_service_health_check_properties(props)
                self._api.service_health_check_update(service_health_check)
            else:
                shc_uuid = str(uuid.uuid4())
                fq_name = pool.name + '-' + shc_uuid
                tenant_id = str(uuid.UUID(hm.parent_uuid.replace('-', '')))
                project = self._api.project_read(id=tenant_id)
                service_health_check = ServiceHealthCheck(
                                              name=fq_name,
                                              parent_obj=project,
                                              service_health_check_properties=props)
                service_health_check.uuid = shc_uuid
                self._api.service_health_check_create(service_health_check)

                for member_id in pool.members:
                    member = LoadbalancerMemberSM.get(member_id)
                    if member is None:
                        continue
                    self._add_service_health_check_ref(service_health_check, member.vmi)

            pool.service_health_check = service_health_check
            for member_id in pool.members:
                member = LoadbalancerMemberSM.get(member_id)
                if member:
                    member.service_health_check = service_health_check
            hm.service_health_check_id = service_health_check.uuid
            hm.provider = pool.provider

            driver_data = {}
            driver_data['provider'] = hm.provider
            driver_data['service_health_check_id'] = hm.service_health_check_id
            driver_data['pool_id'] = pool_id
            self.db.health_monitor_driver_info_insert(hm_id, driver_data)
        except NoIdError:
            return

    def _clear_health_monitor_props(self, hm_id, pool_id):
        driver_data = self.db.health_monitor_driver_info_get(hm_id)
        if driver_data is None:
            return

        try:
            id = driver_data['service_health_check_id']
            service_health_check = self._api.service_health_check_read(id=id)
            vmi_back_refs = service_health_check.get_virtual_machine_interface_back_refs()
            for vmi in vmi_back_refs or []:
                self._api.ref_update('virtual-machine-interface', vmi['uuid'], \
                     'service-health-check', service_health_check.uuid, None, 'DELETE')
            self._api.service_health_check_update(service_health_check)
            self._api.service_health_check_delete(id=service_health_check.uuid)
            if driver_data['pool_id']:
                pool = LoadbalancerPoolSM.get(driver_data['pool_id'])
                if pool is None:
                    return
                pool.service_health_check = None
                for member_id in pool.members:
                    member = LoadbalancerMemberSM.get(member_id)
                    if member is None:
                        continue
                    member.service_health_check = None
        except NoIdError:
            return

    def create_loadbalancer(self, loadbalancer):
        self._update_loadbalancer_props(loadbalancer['id'])

    def update_loadbalancer(self, old_loadbalancer, loadbalancer):
        self._update_loadbalancer_props(loadbalancer['id'])

    def suspend_loadbalancer(self, loadbalancer):
        self._clear_loadbalancer_props(loadbalancer['id'])

    def delete_loadbalancer(self, loadbalancer):
        self._clear_loadbalancer_props(loadbalancer['id'])

    def create_listener(self, listener):
        self._update_listener_props(None, listener)

    def update_listener(self, old_listener, listener):
        self._update_listener_props(old_listener, listener)

    def delete_listener(self, listener):
        self._clear_listener_props(listener['id'])

    def create_pool(self, pool):
        self._update_pool_props(pool['id'])

    def update_pool(self, old_pool, pool):
        self._update_pool_props(pool['id'])

    def delete_pool(self, pool):
        self._clear_pool_props(pool['id'])

    def create_member(self, member):
        self._update_member_props(member['id'])

    def update_member(self, old_member, member):
        self._update_member_props(member['id'])

    def delete_member(self, member):
        self._clear_member_props(member['id'])

    def create_health_monitor(self,
                              health_monitor,
                              pool_id):
        self._update_health_monitor_props(health_monitor['id'], pool_id)

    def update_health_monitor(self,
                              old_health_monitor,
                              health_monitor,
                              pool_id):
        self._update_health_monitor_props(health_monitor['id'], pool_id)

    def delete_health_monitor(self, health_monitor, pool_id):
        self._clear_health_monitor_props(health_monitor['id'], pool_id)

    def stats(self, pool_id):
        pass

    def create_vip(self, vip):
        pass

    def update_vip(self, old_vip, vip):
        pass

    def delete_vip(self, vip):
        pass

