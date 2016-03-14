from mako.template import Template
from mako import exceptions
from mako.runtime import Context
from StringIO import StringIO
from svc_monitor.config_db import *

PROTO_MAP = {
    'TCP': 'tcp',
    'HTTP': 'http',
    'HTTPS': 'http'
}

LB_METHOD_MAP = {
    'ROUND_ROBIN': 'roundrobin',
    'LEAST_CONNECTIONS': 'leastconn',
    'SOURCE_IP': 'source'
}

class HaproxyConfig(object):
    def __init__(self):
        self.config = {}
        self.template = Template(filename='haproxy_template.txt', format_exceptions=True)
        self.conf_dir = '/var/lib/contrail/loadbalancer/'

    def build_config_v1(self, pool_dict):
        pool = LoadbalancerPoolSM.get(pool_dict['id'])
        if not pool:
            return
        self.set_globals(pool.uuid)
        self.set_defaults()
        self.set_virtual_ip(pool)
        buf = StringIO()
        ctx = Context(buf, **self.config)
        self.template.render_context(ctx)
        return buf.getvalue()

    def build_config_v2(self, lb):
        self.set_globals(lb.uuid)
        self.set_defaults()
        self.set_loadbalancer(lb)
        buf = StringIO()
        ctx = Context(buf, **self.config)
        self.template.render_context(ctx)
        return buf.getvalue()

    def set_globals(self, uuid):
        self.config['sock_path'] = self.conf_dir + uuid + '/haproxy.sock'
        self.config['max_conn'] = 65000
        self.config['ssl_ciphers'] = \
            'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:' \
            'ECDH+AES128:DH+AES:ECDH+3DES:DH+3DES:RSA+AESGCM:' \
            'RSA+AES:RSA+3DES:!aNULL:!MD5:!DSS'

    def set_defaults(self):
        self.config['client_timeout'] = "300000"
        self.config['server_timeout'] = "300000"
        self.config['connect_timeout'] = "5000"

    def set_loadbalancer(self, lb):
        loadbalancer = {}
        loadbalancer['vip'] = lb.virtual_ip
        loadbalancer['listeners'] = []
        for listener_id in lb.listeners:
            listener = LoadbalancerListener.get(listener_id)
            if not listener:
                continue
            loadbalancer['listeners'].append(listener_id)
            self.add_listener(listener)
        self.config['loadbalancer'] = loadbalancer

    def add_listener(self, ll):
        listener = {}
        listener['port'] = ll.port
        listener['protocol'] = PROTO_MAP.get(ll.protocol)
        for pool_id in ll.pools:
            pool = LoadbalancerPool.get(pool_id)
            if not pool:
                continue
            listener['pool'] = pool_id
            self.add_pool(pool)
        self.config['listeners'][ll.uuid] = listener

    def set_virtual_ip(self, pool):
        vip = VirtualIpSM.get(pool.virtual_ip)
        if not vip:
            return
        loadbalancer = {}
        loadbalancer['vip'] = vip.params['address']
        loadbalancer['listeners'] = [vip.uuid]
        self.config['loadbalancer'] = loadbalancer
        self.config['persistence'] = vip.params['persistence_type']
        self.config['cookie'] = vip.params['persistence_cookie_name']
        self.config['ssl_cert'] = '/tmp/ssl_cert'
        self.config['listeners'] = {vip.uuid :
            {'port': vip.params['protocol_port'],
             'protocol': PROTO_MAP.get(vip.params['protocol']),
             'pool': pool.uuid}}
        self.config['pools'] = {}
        self.add_pool(pool)

    def add_pool(self, lp):
        pool = {}
        pool['protocol'] = PROTO_MAP.get(lp.params['protocol'])
        pool['method'] = LB_METHOD_MAP.get(lp.params['loadbalancer_method'])
        pool['members'] = []
        self.set_healthmonitor(lp)
        self.config['members'] = {}
        for server_id in lp.members:
            server = LoadbalancerMemberSM.get(server_id)
            if not server:
                continue
            pool['members'].append(server_id)
            self.add_server(server)
        self.config['pools'][lp.uuid] = pool

    def add_server(self, server):
        member = {}
        member['address'] = server.params['address']
        member['port'] = server.params['protocol_port']
        member['weight'] = server.params['weight']
        self.config['members'][server.uuid] = member

    def set_healthmonitor(self, pool):
        for hm_id in pool.loadbalancer_healthmonitors:
            hm = HealthMonitorSM.get(hm_id)
            if not hm:
                continue
            self.config['monitor_type'] = hm.params['monitor_type']
            self.config['timeout'] = hm.params['timeout']
            self.config['delay'] = hm.params['delay']
            self.config['max_retries'] = hm.params['max_retries']
            self.config['http_method'] = hm.params['http_method']
            self.config['url_path'] = hm.params['url_path']
            self.config['expected_codes'] = \
                '|'.join(self._get_codes(hm.params['expected_codes']))
            return

    def _get_codes(self, codes):
        response = set()
        for code in codes.replace(',', ' ').split(' '):
            code = code.strip()
            if not code:
                continue
            elif '-' in code:
                low, hi = code.split('-')[:2]
                response.update(str(i) for i in xrange(int(low), int(hi) + 1))
            else:
                response.add(code)
        return response
