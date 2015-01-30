from vnc_api.vnc_api import *

from cfgm_common import importutils

from neutron.api.v2 import attributes
from neutron.plugins.common import constants

from config_db import *

class LoadbalancerAgent(object):
    def __init__(self, svc_mon, vnc_lib, config_section):
        # Loadbalancer
        self._vnc_lib = vnc_lib
        self._svc_mon = svc_mon
        self._pool_driver = {}
        self._loadbalancer_driver = {}
        self._default_provider = self._load_driver(config_section)
    # end __init__

    def _load_driver(self, config_section):
        # TODO Load the driver fom config option
        self._loadbalancer_driver["opencontrail"] = importutils.import_object(
            "svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.OpencontrailLoadbalancerDriver", self._svc_mon, self._vnc_lib, config_section)
        self._loadbalancer_driver["f5"] = importutils.import_object(
            "svc_monitor.services.loadbalancer.drivers.f5.f5_driver.OpencontrailF5LoadbalancerDriver", self._svc_mon, self._vnc_lib, config_section)
        return "opencontrail"
    # end _load_driver

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

    # Loadbalancer
    def loadbalancer_pool_add(self, pool):
        p = self.loadbalancer_pool_get_reqdict(pool)
        driver = self._get_driver_for_pool(p['id'], p['provider'])
        try:
            if not pool.last_sent:
                driver.create_pool(p)
            #elif p != pool.last_sent:
            else:
                driver.update_pool(pool.last_sent, p)
        except Exception as ex:
            pass
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
        except Exception as ex:
            pass
        return m
    # end loadbalancer_member_add

    def virtual_ip_add(self, vip):
        v = self.virtual_ip_get_reqdict(vip)
        driver = self._get_driver_for_pool(v['pool_id'])
        try:
            if not vip.last_sent:
                driver.create_vip(v)
            elif v != vip.last_sent:
                driver.update_vip(vip.last_sent, v)
        except Exception as ex:
            pass
        return v
    # end  virtual_ip_add

    def delete_virtual_ip(self, obj):
        v = obj.last_sent
        driver = self._get_driver_for_pool(v['pool_id'])
        try:
            driver.delete_vip(v)
        except Exception as ex:
            pass
    # end delete_virtual_ip

    def delete_loadbalancer_member(self, obj):
        m = obj.last_sent
        driver = self._get_driver_for_pool(m['pool_id'])
        try:
            driver.delete_member(m)
        except Exception as ex:
            pass
    # end delete_loadbalancer_member

    def delete_loadbalancer_pool(self, obj):
        p = obj.last_sent
        driver = self._get_driver_for_pool(p['id'])
        try:
            driver.delete_pool(p)
        except Exception as ex:
            pass
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
        except Exception as ex:
            pass
    # end update_hm

    def _get_vip_pool_id(self, vip):
        pool_refs = vip.loadbalancer_pool
        if pool_refs is None:
            return None
        return pool_refs
    # end _get_vip_pool_id

    def _get_interface_params(self, vip, props):
        port_id = vip.virtual_machine_interface
        if port_id is None:
            return None

        if not props['address'] or props['address'] == attributes.ATTR_NOT_SPECIFIED:
            vmi = VirtualMachineInterfaceSM.get(port_id)
            ip_refs = vmi.instance_ip
            if ip_refs:
                iip = InstanceIpSM.get(ip_refs)
                props['address'] = iip.address

        return port_id
    # end _get_interface_params

    def virtual_ip_get_reqdict(self, vip):
        props = vip.params
        port_id = self._get_interface_params(vip, props)

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

        return res;
    # end virtual_ip_get_reqdict

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
            return constants.ACTIVE
        return constants.PENDING_DELETE
    # end _get_object_status

    def loadbalancer_pool_get_reqdict(self, pool):
        res = {
            'id': pool.uuid,
            'tenant_id': pool.parent_uuid.replace('-', ''),
            'name': pool.display_name,
            'description': self._get_object_description(pool),
            'status': self._get_object_status(pool),
        }

        props = pool.params
        for key, mapping in self._loadbalancer_pool_type_mapping.iteritems():
            value = props[key]
            if value is not None:
                res[mapping] = value

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
