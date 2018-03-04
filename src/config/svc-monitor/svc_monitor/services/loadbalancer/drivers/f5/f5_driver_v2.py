import sys
from oslo_config import cfg

from f5_openstack_agent.lbaasv2.drivers.bigip import icontrol_driver
from f5_openstack_agent.lbaasv2.drivers.bigip import agent_manager
from svc_monitor.services.loadbalancer.drivers import abstract_driver
from svc_monitor.config_db import *

class OpencontrailF5LoadbalancerDriver(
        abstract_driver.ContrailLoadBalancerAbstractDriver):

    def __init__(self, name, manager, api, db, args=None):
        self._name = name
        self._manager = manager
        self._api = api
        self.db = db
        args_driver = dict(args.config_sections.items(self._name))
        args_auth = dict(args.config_sections.items('KEYSTONE'))
        self.service = {}

        conf = cfg.ConfigOpts()
        conf.register_opts(agent_manager.OPTS)
        conf.register_opts(icontrol_driver.OPTS)
        conf.register_opt(cfg.BoolOpt('debug', default = False))
        conf.f5_global_routed_mode = True
        if not 'device_ip' in args_driver:
            return None
        if 'ha_mode' in args_driver:
            conf.f5_ha_type = args_driver['ha_mode']
        conf.icontrol_hostname = args_driver['device_ip']
        conf.icontrol_username = args_driver['user']
        conf.icontrol_password = args_driver['password']
        conf.cert_manager = 'f5_openstack_agent.lbaasv2.drivers.bigip' \
                '.barbican_cert.BarbicanCertManager'
        if 'v2.0' in args_auth['auth_url']:
            conf.auth_version = 'v2'
            conf.os_username = args_auth['admin_user']
            conf.os_password = args_auth['admin_password']
            conf.os_auth_url = args_auth['auth_url']
            conf.os_tenant_name = args_auth['admin_tenant_name']
        elif 'v3' in args_auth['auth_url']:
            conf.auth_version = 'v3'
            conf.os_username = args_auth['admin_user']
            conf.os_password = args_auth['admin_password']
            conf.os_auth_url = args_auth['auth_url']
            conf.os_project_name = args_auth['admin_tenant_name']
            conf.os_user_domain_name = 'default'
            conf.os_project_domain_name = 'default'

        self.icontrol = icontrol_driver.iControlDriver(conf,
                registerOpts = False)

    def _service_lb(self, lb_id, status = 'ACTIVE'):
        self.service = {}
        lb = LoadbalancerSM.get(lb_id)
        if not lb:
            self.service['loadbalancer'] = None
            return
        tenant = ProjectSM.get(lb.parent_uuid)
        vmi = VirtualMachineInterfaceSM.get(lb.virtual_machine_interface)
        vn = VirtualNetworkSM.get(vmi.virtual_network)
        conf = {'id': lb.uuid,
                'config': None,
                'tenant_id': tenant.name,
                'name': lb.name,
                'description': "",
                'vip_subnet_id': lb.params['vip_subnet_id'],
                'vip_address': lb.params['vip_address'],
                'port_id': lb.virtual_machine_interface,
                'provisioning_status': status,
                'status': lb.params['status'],
                'network_id': vn.name}
        self.service['loadbalancer'] = conf
        self.service['networks'] = {vn.name: {'provider:network_type': 'flat'}}

    def _service_listener(self, l_id, status = 'ACTIVE'):
        lb_conf = self.service['loadbalancer']
        l = LoadbalancerListenerSM.get(l_id)
        conf = {'id': lb_conf['name'] + '_' + l.name,
                'config': None,
                'tenant_id': lb_conf['tenant_id'],
                'name': l.name,
                'protocol': l.params['protocol'],
                'protocol_port': l.params['protocol_port'],
                'provisioning_status': status,
                'admin_state_up': l.params['admin_state'],
                'description': ""}
        if l.params['protocol'] == 'TERMINATED_HTTPS':
            conf['default_tls_container_id'] = l.params['default_tls_container']
            conf['sni_containers'] = []
            for sni_container in l.params['sni_containers']:
                c = {'tls_container_id': sni_container}
                conf['sni_containers'].append(c)

        self.service['listeners'] = [conf]

    def _service_pool(self, p_id, status = 'ACTIVE'):
        lb_conf = self.service['loadbalancer']
        l_conf = self.service['listeners'][0]
        p = LoadbalancerPoolSM.get(p_id)
        conf = {'id': lb_conf['name'] + '_' + p.name,
                'config': None,
                'tenant_id': lb_conf['tenant_id'],
                'provisioning_status': status,
                'listeners': [{'id': l_conf['id']}],
                'description': ""}
        self.service['pools'] = [conf]

    def _service_member(self, m_id, status = 'ACTIVE'):
        lb_conf = self.service['loadbalancer']
        p_conf = self.service['pools'][0]
        m = LoadbalancerMemberSM.get(m_id)
        conf = {'id': m_id,
                'config': None,
                'tenant_id': lb_conf['tenant_id'],
                'provisioning_status': status,
                'pool_id': p_conf['id'],
                'address': m.params['address'],
                'protocol_port': m.params['protocol_port'],
                'subnet_id': None,
                'network_id': None,
                'description': ""}
        self.service['members'] = [conf]

    def _service_healthmonitor(self, hm_id, status = 'ACTIVE'):
        lb_conf = self.service['loadbalancer']
        p_conf = self.service['pools'][0]
        hm = HealthMonitorSM.get(hm_id)
        conf = {'id': hm_id,
                'config': None,
                'tenant_id': lb_conf['tenant_id'],
                'provisioning_status': status,
                'pool_id': p_conf['id'],
                'type': hm.params['monitor_type'],
                'url_path': hm.params['url_path'],
                'delay': hm.params['delay'],
                'max_retries': hm.params['max_retries'],
                'timeout': hm.params['timeout'],
                'expected_codes': hm.params['expected_codes'],
                'description': ""}
        self.service['healthmonitors'] = [conf]

    def create_loadbalancer(self, loadbalancer):
        self._service_lb(loadbalancer['id'])
        if self.service['loadbalancer']:
            self.icontrol.create_loadbalancer(None, self.service)

    def update_loadbalancer(self, old_loadbalancer, loadbalancer):
        pass

    def delete_loadbalancer(self, loadbalancer):
        self._service_lb(loadbalancer['id'], status = 'PENDING_DELETE')
        self.icontrol.delete_loadbalancer(None, self.service)

    def create_listener(self, listener):
        self._service_lb(listener['loadbalancer_id'])
        self._service_listener(listener['id'])
        self.icontrol.create_listener(None, self.service)

    def update_listener(self, old_listener, listener):
        pass

    def delete_listener(self, listener):
        self._service_lb(listener['loadbalancer_id'])
        self._service_listener(listener['id'], status = 'PENDING_DELETE')
        self.icontrol.delete_listener(None, self.service)

    def create_pool(self, pool):
        self._service_lb(pool['loadbalancer_id'])
        pool_sm = LoadbalancerPoolSM.get(pool['id'])
        self._service_listener(pool_sm.loadbalancer_listener)
        self._service_pool(pool['id'])
        self.icontrol.create_pool(None, self.service)

    def update_pool(self, old_pool, pool):
        pass

    def delete_pool(self, pool):
        self._service_lb(pool['loadbalancer_id'])
        pool_sm = LoadbalancerPoolSM.get(pool['id'])
        self._service_listener(pool_sm.loadbalancer_listener)
        self._service_pool(pool['id'], status = 'PENDING_DELETE')
        self.icontrol.delete_pool(None, self.service)

    def create_member(self, member):
        pool_sm = LoadbalancerPoolSM.get(member['pool_id'])
        listener_sm = LoadbalancerListenerSM.get(pool_sm.loadbalancer_listener)
        self._service_lb(listener_sm.loadbalancer)
        self._service_listener(listener_sm.uuid)
        self._service_pool(pool_sm.uuid)
        self._service_member(member['id'])
        self.icontrol.create_member(None, self.service)

    def update_member(self, old_member, member):
        pass

    def delete_member(self, member):
        pool_sm = LoadbalancerPoolSM.get(member['pool_id'])
        listener_sm = LoadbalancerListenerSM.get(pool_sm.loadbalancer_listener)
        self._service_lb(listener_sm.loadbalancer)
        self._service_listener(listener_sm.uuid)
        self._service_pool(pool_sm.uuid)
        self._service_member(member['id'], status = 'PENDING_DELETE')
        self.icontrol.delete_member(None, self.service)

    def create_health_monitor(self, health_monitor, pool_id):
        pool_sm = LoadbalancerPoolSM.get(pool_id)
        listener_sm = LoadbalancerListenerSM.get(pool_sm.loadbalancer_listener)
        self._service_lb(pool_sm.loadbalancer_id)
        self._service_listener(listener_sm.uuid)
        self._service_pool(pool_id)
        self._service_healthmonitor(health_monitor['id'])
        self.icontrol.create_health_monitor(None, self.service)

    def update_health_monitor(self, old_health_monitor, health_monitor,
            pool_id):
        pass

    def delete_health_monitor(self, health_monitor, pool_id):
        pool_sm = LoadbalancerPoolSM.get(pool_id)
        listener_sm = LoadbalancerListenerSM.get(pool_sm.loadbalancer_listener)
        self._service_lb(pool_sm.loadbalancer_id)
        self._service_listener(listener_sm.uuid)
        self._service_pool(pool_id)
        self._service_healthmonitor(health_monitor['id'],
                status = 'PENDING_DELETE')
        self.icontrol.delete_health_monitor(None, self.service)

    def create_vip(self, vip):
        pass

    def update_vip(self, old_vip, vip):
        pass

    def delete_vip(self, vip):
        pass

    def stats(self, pool_id):
        pass

    def set_config_v2(self, lb_id):
        pass

