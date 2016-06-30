from vnc_api.vnc_api import *

from cfgm_common import importutils
from cfgm_common import exceptions as vnc_exc
from cfgm_common import svc_info

from agent import Agent
from config_db import ServiceApplianceSM, ServiceApplianceSetSM, \
    LoadbalancerPoolSM, InstanceIpSM, VirtualMachineInterfaceSM, \
    VirtualIpSM, LoadbalancerSM, LoadbalancerListenerSM


class LoadbalancerAgent(Agent):

    def __init__(self, svc_mon, vnc_lib, cassandra, config_section):
        # Loadbalancer
        super(LoadbalancerAgent, self).__init__(svc_mon, vnc_lib,
                                                cassandra, config_section)
        self._vnc_lib = vnc_lib
        self._svc_mon = svc_mon
        self._cassandra = cassandra
        self._pool_driver = {}
        self._args = config_section
        self._loadbalancer_driver = {}
        # create default service appliance set
        self._create_default_service_appliance_set(
            "opencontrail",
            "svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.OpencontrailLoadbalancerDriver"
        )
        self._default_provider = "opencontrail"
    # end __init__

    def handle_service_type(self):
        return svc_info.get_lb_service_type()

    def pre_create_service_vm(self, instance_index, si, st, vm):
        for nic in si.vn_info:
            if nic['type'] == svc_info.get_right_if_str():
                vmi = self._get_vip_vmi(si)
                if not vmi:
                    return False
                for iip_id in vmi.instance_ips:
                    nic['iip-id'] = iip_id
                    break
                for fip_id in vmi.floating_ips:
                    nic['fip-id'] = fip_id
                    break
                if len(vmi.security_groups):
                    nic['sg-list'] = vmi.security_groups
                    break
                nic['user-visible'] = False
            elif nic['type'] == svc_info.get_left_if_str():
                nic['user-visible'] = False

        return True

    def _get_vip_vmi(self, si):
        lb = LoadbalancerSM.get(si.loadbalancer)
        if lb:
            vmi_id = lb.virtual_machine_interface
            vmi = VirtualMachineInterfaceSM.get(vmi_id)
            return vmi

        pool = LoadbalancerPoolSM.get(si.loadbalancer_pool)
        if pool:
            vip = VirtualIpSM.get(pool.virtual_ip)
            if vip:
                vmi_id = vip.virtual_machine_interface
                vmi = VirtualMachineInterfaceSM.get(vmi_id)
                return vmi

        return None

    # create default loadbalancer driver
    def _create_default_service_appliance_set(self, sa_set_name, driver_name):
        default_gsc_name = "default-global-system-config"
        default_gsc_fq_name = [default_gsc_name]
        sa_set_fq_name = [default_gsc_name, sa_set_name]

        try:
            sa_set_obj = self._vnc_lib.service_appliance_set_read(fq_name=sa_set_fq_name)
        except vnc_exc.NoIdError:
            gsc_obj = self._vnc_lib.global_system_config_read(fq_name=default_gsc_fq_name)
            sa_set_obj = ServiceApplianceSet(sa_set_name, gsc_obj)
            sa_set_obj.set_service_appliance_driver(driver_name)
            sa_set_uuid = self._vnc_lib.service_appliance_set_create(sa_set_obj)
            ServiceApplianceSetSM.locate(sa_set_uuid)

    def load_drivers(self):
        for sas in ServiceApplianceSetSM.values():
            if sas.driver:
                config = self._args.config_sections
                config.add_section(sas.name)
                for kvp in sas.kvpairs or []:
                    config.set(sas.name, kvp['key'], kvp['value'])
                if sas.ha_mode:
                    config.set(sas.name, 'ha_mode', str(sas.ha_mode))
                for sa in sas.service_appliances or []:
                    saobj = ServiceApplianceSM.get(sa)
                    config.set(sas.name, 'device_ip', saobj.ip_address)
                    config.set(sas.name, 'user',
                               saobj.user_credential['username'])
                    config.set(sas.name, 'password',
                               saobj.user_credential['password'])
                self._loadbalancer_driver[sas.name] = \
                    importutils.import_object(sas.driver, sas.name,
                                              self._svc_mon, self._vnc_lib,
                                              self._cassandra, self._args)
    # end load_drivers

    def audit_lb_pools(self):
        for pool_id, config_data, driver_data in self._cassandra.pool_list():
            if LoadbalancerPoolSM.get(pool_id):
                continue
            # Delete the pool from the driver
            driver = self._get_driver_for_provider(config_data['provider'])
            driver.delete_pool(config_data)
            self._cassandra.pool_remove(pool_id)

    def load_driver(self, sas):
        if sas.name in self._loadbalancer_driver:
            del(self._loadbalancer_driver[sas.name])
        if sas.driver:
            config = self._args.config_sections
            try:
                config.remove_section(sas.name)
            except Exception:
                pass
            config.add_section(sas.name)
            for kvp in sas.kvpairs or []:
                config.set(sas.name, kvp['key'], kvp['value'])
            if sas.ha_mode:
                config.set(sas.name, 'ha_mode', sas.ha_mode)
            for sa in sas.service_appliances or []:
                saobj = ServiceApplianceSM.get(sa)
                config.set(sas.name, 'device_ip', saobj.ip_address)
                config.set(sas.name, 'user', saobj.user_credential['username'])
                config.set(sas.name, 'password',
                           saobj.user_credential['password'])
            self._loadbalancer_driver[sas.name] = \
                importutils.import_object(sas.driver, sas.name,
                                          self._svc_mon, self._vnc_lib,
                                          self._cassandra, self._args)
    # end load_driver

    def unload_driver(self, sas):
        if sas.name not in self._loadbalancer_driver:
            return
        del(self._loadbalancer_driver[sas.name])
    # end unload_driver

    def _get_driver_for_provider(self, provider_name):
        return self._loadbalancer_driver[provider_name]
    # end _get_driver_for_provider

    def _get_driver_for_pool(self, pool_id, provider=None):
        if not pool_id:
            return self.drivers[self._default_provider]
        if pool_id in self._pool_driver:
            return self._pool_driver[pool_id]
        if not provider:
            pool = LoadbalancerPoolSM.get(pool_id)
            provider = pool.provider
        driver = self._get_driver_for_provider(provider)
        self._pool_driver[pool_id] = driver
        return driver
    # end _get_driver_for_pool

    def _get_driver_for_loadbalancer(self, lb_id, provider=None):
        if not lb_id:
            return self.drivers[self._default_provider]
        if lb_id in self._loadbalancer_driver:
            return self._loadbalancer_driver[lb_id]
        if not provider:
            lb = LoadbalancerSM.get(lb_id)
            provider = lb.provider
        driver = self._get_driver_for_provider(provider)
        self._loadbalancer_driver[lb_id] = driver
        return driver

    # Loadbalancer
    def loadbalancer_pool_add(self, pool):
        p = self.loadbalancer_pool_get_reqdict(pool)
        driver = self._get_driver_for_pool(p['id'], p['provider'])
        try:
            if p['loadbalancer_id']:
                driver.set_config_v2(p['loadbalancer_id'])
            if not pool.last_sent:
                driver.create_pool(p)
            #elif p != pool.last_sent:
            else:
                driver.update_pool(pool.last_sent, p)
        except Exception:
            pass
        self._cassandra.pool_config_insert(p['id'], p)
        return p
    # end loadbalancer_pool_add

    def loadbalancer_member_add(self, member):
        m = self.loadbalancer_member_get_reqdict(member)
        driver = self._get_driver_for_pool(m['pool_id'])
        try:
            if not member.last_sent:
                driver.create_member(m)
            elif m != member.last_sent:
                driver.update_member(member.last_sent, m)
        except Exception:
            pass
        return m
    # end loadbalancer_member_add

    def virtual_ip_add(self, vip):
        v = self.virtual_ip_get_reqdict(vip)
        driver = self._get_driver_for_pool(v['pool_id'])
        try:
            driver.set_config_v1(vip.loadbalancer_pool)
            if not vip.last_sent:
                driver.create_vip(v)
            elif v != vip.last_sent:
                driver.update_vip(vip.last_sent, v)
        except Exception:
            pass
        return v
    # end  virtual_ip_add

    def delete_virtual_ip(self, obj):
        v = obj.last_sent
        driver = self._get_driver_for_pool(v['pool_id'])
        try:
            driver.delete_vip(v)
        except Exception:
            pass
    # end delete_virtual_ip

    def loadbalancer_add(self, loadbalancer):
        lb = self.loadbalancer_get_reqdict(loadbalancer)
        driver = self._get_driver_for_loadbalancer(lb['id'], 'opencontrail')
        try:
            lbaas_config = driver.set_config_v2(loadbalancer.uuid)
            lb['config'] = lbaas_config
            if not loadbalancer.last_sent:
                driver.create_loadbalancer(lb)
            elif lb != loadbalancer.last_sent:
                driver.update_loadbalancer(loadbalancer.last_sent, lb)
        except Exception:
            pass
        return lb

    def delete_loadbalancer(self, loadbalancer):
        lb = self.loadbalancer_get_reqdict(loadbalancer)
        driver = self._get_driver_for_loadbalancer(lb['id'], 'opencontrail')
        try:
            driver.delete_loadbalancer(lb)
        except Exception:
            pass

    def listener_add(self, listener):
        ll = self.listener_get_reqdict(listener)
        driver = self._get_driver_for_loadbalancer(ll['loadbalancer_id'])
        try:
            if not listener.last_sent:
                driver.create_listener(ll)
            elif ll != listener.last_sent:
                driver.update_listener(listener.last_sent, ll)
        except Exception:
            pass
        return ll

    def delete_listener(self, listener):
        ll = self.listener_get_reqdict(listener)
        driver = self._get_driver_for_loadbalancer(ll['loadbalancer_id'])
        try:
            driver.delete_listener(ll)
        except Exception:
            pass

    def delete_loadbalancer_member(self, obj):
        m = obj.last_sent
        driver = self._get_driver_for_pool(m['pool_id'])
        try:
            driver.delete_member(m)
        except Exception:
            pass
    # end delete_loadbalancer_member

    def delete_loadbalancer_pool(self, obj):
        p = obj.last_sent
        driver = self._get_driver_for_pool(p['id'])
        try:
            driver.delete_pool(p)
        except Exception:
            pass
        self._cassandra.pool_remove(p['id'])
    # end delete_loadbalancer_pool

    def update_hm(self, obj):
        hm = self.hm_get_reqdict(obj)
        current_pools = hm['pools'] or []
        old_pools = []
        if obj.last_sent:
            old_pools = hm['pools'] or []

        set_current_pools = set()
        set_old_pools = set()
        for i in current_pools:
            set_current_pools.add(i['pool_id'])
        for i in old_pools:
            set_old_pools.add(i['pool_id'])
        update_pools = set_current_pools & set_old_pools
        delete_pools = set_old_pools - set_current_pools
        add_pools = set_current_pools - set_old_pools
        try:
            for pool in add_pools:
                driver = self._get_driver_for_pool(pool)
                driver.create_pool_health_monitor(hm, pool)
            for pool in delete_pools:
                driver = self._get_driver_for_pool(pool)
                driver.delete_pool_health_monitor(hm, pool)
            for pool in update_pools:
                driver = self._get_driver_for_pool(pool)
                driver.update_health_monitor(obj.last_sent,
                                             hm, pool)
        except Exception:
            pass
    # end update_hm

    def _get_vip_pool_id(self, vip):
        pool_refs = vip.loadbalancer_pool
        if pool_refs is None:
            return None
        return pool_refs
    # end _get_vip_pool_id

    def _get_interface_params(self, port_id, props):
        if port_id is None:
            return None

        if not props['address']:
            vmi = VirtualMachineInterfaceSM.get(port_id)
            for iip_id in vmi.instance_ips:
                iip = InstanceIpSM.get(iip_id)
                props['address'] = iip.address
                break

        return port_id
    # end _get_interface_params

    def virtual_ip_get_reqdict(self, vip):
        props = vip.params
        port_id = self._get_interface_params(vip.virtual_machine_interface,
            props)

        res = {'id': vip.uuid,
               'tenant_id': vip.parent_uuid.replace('-', ''),
               'name': vip.display_name,
               'description': self._get_object_description(vip),
               'subnet_id': props['subnet_id'],
               'address': props['address'],
               'port_id': port_id,
               'protocol_port': props['protocol_port'],
               'protocol': props['protocol'],
               'pool_id': self._get_vip_pool_id(vip),
               'session_persistence': None,
               'connection_limit': props['connection_limit'],
               'admin_state_up': props['admin_state'],
               'status': self._get_object_status(vip)}

        if props['persistence_type']:
            sp = {'type': props['persistence_type']}
            if props['persistence_type'] == 'APP_COOKIE':
                sp['cookie_name'] = props['persistence_cookie_name']
            res['session_persistence'] = sp

        return res
    # end virtual_ip_get_reqdict

    def loadbalancer_get_reqdict(self, lb):
        props = lb.params
        res = {'id': lb.uuid,
               'config': None,
               'tenant_id': lb.parent_uuid.replace('-', ''),
               'name': lb.display_name,
               'description': self._get_object_description(lb),
               'subnet_id': props['vip_subnet_id'],
               'address': props['vip_address'],
               'port_id': lb.virtual_machine_interface,
               'status': self._get_object_status(lb)}

        return res
    # end loadbalancer_get_reqdict

    def listener_get_reqdict(self, listener):
        props = listener.params

        res = {'id': listener.uuid,
               'tenant_id': listener.parent_uuid.replace('-', ''),
               'name': listener.display_name,
               'description': self._get_object_description(listener),
               'protocol_port': props['protocol_port'],
               'protocol': props['protocol'],
               'loadbalancer_id': listener.loadbalancer,
               'admin_state_up': props['admin_state'],
               'connection_limit': props['connection_limit'],
               'default_tls_container': props['default_tls_container'],
               'sni_containers': props['sni_containers'],
               'status': self._get_object_status(listener)}

        return res

    _loadbalancer_health_type_mapping = {
        'admin_state': 'admin_state_up',
        'monitor_type': 'type',
        'delay': 'delay',
        'timeout': 'timeout',
        'max_retries': 'max_retries',
        'http_method': 'http_method',
        'url_path': 'url_path',
        'expected_codes': 'expected_codes'
    }

    def hm_get_reqdict(self, health_monitor):
        res = {'id': health_monitor.uuid,
               'tenant_id': health_monitor.parent_uuid.replace('-', ''),
               'status': self._get_object_status(health_monitor)}

        props = health_monitor.params
        for key, mapping in self._loadbalancer_health_type_mapping.iteritems():
            value = props[key]
            if value is not None:
                res[mapping] = value

        pool_ids = []
        pool_back_refs = health_monitor.loadbalancer_pools
        for pool_back_ref in pool_back_refs or []:
            pool_id = {}
            pool_id['pool_id'] = pool_back_ref
            pool_ids.append(pool_id)
        res['pools'] = pool_ids

        return res
    # end hm_get_reqdict

    _loadbalancer_member_type_mapping = {
        'admin_state': 'admin_state_up',
        'status': 'status',
        'protocol_port': 'protocol_port',
        'weight': 'weight',
        'address': 'address',
    }

    def loadbalancer_member_get_reqdict(self, member):
        res = {'id': member.uuid,
               'pool_id': member.loadbalancer_pool,
               'status': self._get_object_status(member)}

        pool = LoadbalancerPoolSM.get(member.loadbalancer_pool)
        res['tenant_id'] = pool.parent_uuid.replace('-', '')

        props = member.params
        for key, mapping in self._loadbalancer_member_type_mapping.iteritems():
            value = props[key]
            if value is not None:
                res[mapping] = value

        return res
    # end loadbalancer_member_get_reqdict

    _loadbalancer_pool_type_mapping = {
        'admin_state': 'admin_state_up',
        'protocol': 'protocol',
        'loadbalancer_method': 'lb_method',
        'subnet_id': 'subnet_id'
    }

    def _get_object_description(self, obj):
        id_perms = obj.id_perms
        if id_perms is None:
            return None
        return id_perms['description']
    # end _get_object_description

    def _get_object_status(self, obj):
        id_perms = obj.id_perms
        if id_perms and id_perms['enable']:
            return "ACTIVE"
        return "PENDING_DELETE"
    # end _get_object_status

    def loadbalancer_pool_get_reqdict(self, pool):
        res = {
            'id': pool.uuid,
            'loadbalancer_id': pool.loadbalancer_id,
            'tenant_id': pool.parent_uuid.replace('-', ''),
            'name': pool.display_name,
            'description': self._get_object_description(pool),
            'status': self._get_object_status(pool),
            'session_persistence': None,
        }

        props = pool.params
        for key, mapping in self._loadbalancer_pool_type_mapping.iteritems():
            value = props[key]
            if value is not None:
                res[mapping] = value

        if props['session_persistence']:
            sp = {'type': props['session_persistence']}
            if props['session_persistence'] == 'APP_COOKIE':
                sp['cookie_name'] = props['persistence_cookie_name']
            res['session_persistence'] = sp

        # provider
        res['provider'] = pool.provider

        # vip_id
        res['vip_id'] = None
        vip_refs = pool.virtual_ip
        if vip_refs is not None:
            res['vip_id'] = vip_refs

        # members
        res['members'] = list(pool.members)

        # health_monitors
        res['health_monitors'] = list(pool.loadbalancer_healthmonitors)

        # TODO: health_monitor_status
        res['health_monitors_status'] = []
        return res
    # end loadbalancer_pool_get_reqdict
