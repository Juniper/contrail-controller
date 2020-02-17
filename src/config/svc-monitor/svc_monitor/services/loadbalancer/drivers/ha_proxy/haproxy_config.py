from __future__ import absolute_import
from builtins import str
from builtins import range
from svc_monitor.config_db import *
from os.path import dirname, exists, join
import logging
import yaml

try:
    from .custom_attributes.haproxy_validator \
        import validate_custom_attributes as get_valid_attrs
except ImportError:
    custom_attr_dict = {}
    def get_valid_attrs(custom_attr_dict, section, custom_attrs):
        return {}

PROTO_HTTP = 'HTTP'
PROTO_HTTPS = 'HTTPS'
PROTO_TERMINATED_HTTPS = 'TERMINATED_HTTPS'

PROTO_MAP_V1 = {
    'TCP': 'tcp',
    'HTTP': 'http',
    'HTTPS': 'http',
    'TERMINATED_HTTPS': 'http'
}

PROTO_MAP_V2 = {
    'TCP': 'tcp',
    'HTTP': 'http',
    'HTTPS': 'tcp',
    'TERMINATED_HTTPS': 'http'
}

LB_METHOD_MAP = {
    'ROUND_ROBIN': 'roundrobin',
    'LEAST_CONNECTIONS': 'leastconn',
    'SOURCE_IP': 'source'
}

HEALTH_MONITOR_PING = 'PING'
HEALTH_MONITOR_TCP = 'TCP'
HEALTH_MONITOR_HTTP = 'HTTP'
HEALTH_MONITOR_HTTPS = 'HTTPS'

PERSISTENCE_SOURCE_IP = 'SOURCE_IP'
PERSISTENCE_HTTP_COOKIE = 'HTTP_COOKIE'
PERSISTENCE_APP_COOKIE = 'APP_COOKIE'

def get_config_v2(lb):
    custom_attr_dict = get_custom_attributes_dict()
    custom_attrs = get_custom_attributes_v2(lb)
    conf = set_globals(lb.uuid, custom_attr_dict, custom_attrs) + '\n\n'
    conf += set_defaults(custom_attr_dict, custom_attrs) + '\n\n'
    conf += set_v2_frontend_backend(lb, custom_attr_dict, custom_attrs)
    return conf

def get_config_v1(pool):
    custom_attr_dict = get_custom_attributes_dict()
    custom_attrs = get_custom_attributes_v1(pool)
    conf = set_globals(pool.uuid, custom_attr_dict, custom_attrs) + '\n\n'
    conf += set_defaults(custom_attr_dict, custom_attrs) + '\n\n'
    conf += set_v1_frontend_backend(pool, custom_attr_dict, custom_attrs)
    return conf

def get_custom_attributes_dict():
    custom_attr_dict = {}
    script_dir = dirname(__file__)
    rel_path = "custom_attributes/custom_attributes.yml"
    abs_file_path = join(script_dir, rel_path)
    if exists(abs_file_path):
        with open(abs_file_path, 'r') as f:
            custom_attr_dict = yaml.safe_load(f)
    return custom_attr_dict

def get_custom_attributes_v1(pool):
    custom_attrs = {}
    custom_attrs[pool.uuid] = {}
    for kvp in pool.custom_attributes or []:
        custom_attrs[pool.uuid][kvp['key']] = kvp['value']
    return custom_attrs

def get_custom_attributes_v2(lb):
    custom_attrs = {}
    for ll_id in lb.loadbalancer_listeners:
        ll = LoadbalancerListenerSM.get(ll_id)
        if not ll:
            continue
        pool = LoadbalancerPoolSM.get(ll.loadbalancer_pool)
        if pool:
            custom_attrs[pool.uuid] = {}
            for kvp in pool.custom_attributes or []:
                custom_attrs[pool.uuid][kvp['key']] = kvp['value']

    return custom_attrs

def set_globals(uuid, custom_attr_dict, custom_attrs):
    agg_custom_attrs = {}
    for key, value in list(custom_attrs.items()):
        agg_custom_attrs.update(custom_attrs[key])

    global_custom_attrs = get_valid_attrs(custom_attr_dict, 'global',
                                          agg_custom_attrs)
    if 'max_conn' in global_custom_attrs:
        maxconn = global_custom_attrs.pop('max_conn', None)
    else:
        maxconn = 65000

    if 'ssl_ciphers' in global_custom_attrs:
        ssl_ciphers = global_custom_attrs.pop('ssl_ciphers', None)
    else:
        ssl_ciphers = \
            'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:' \
            'ECDH+AES128:DH+AES:ECDH+3DES:DH+3DES:RSA+AESGCM:' \
            'RSA+AES:RSA+3DES:!aNULL:!MD5:!DSS'

    logger_socket = '/var/run/contrail/loadbalancer/haproxy.log.sock'

    conf = [
        'global',
        'daemon',
        'user haproxy',
        'group haproxy',
        'log %s local0' % logger_socket,
        'log %s local1 notice' % logger_socket,
        'tune.ssl.default-dh-param 2048',
        'ssl-default-bind-ciphers %s' % ssl_ciphers,
        'ulimit-n 200000',
        'maxconn %d' % maxconn
    ]

    sock_path = '/var/lib/contrail/loadbalancer/haproxy/'
    sock_path += uuid + '/haproxy.sock'
    conf.append('stats socket %s mode 0666 level user' % sock_path)

    # Adding custom_attributes config
    for key, value in list(global_custom_attrs.items()):
        cmd = custom_attr_dict['global'][key]['cmd']
        conf.append(cmd % value)

    res = "\n\t".join(conf)
    return res

def set_defaults(custom_attr_dict, custom_attrs):
    agg_custom_attrs = {}
    for key, value in list(custom_attrs.items()):
        agg_custom_attrs.update(custom_attrs[key])
    default_custom_attrs = get_valid_attrs(custom_attr_dict, 'default',
                                           agg_custom_attrs)
    if 'client_timeout' in default_custom_attrs:
        client_timeout = default_custom_attrs.pop('client_timeout', None)
    else:
        client_timeout = 300000

    if 'server_timeout' in default_custom_attrs:
        server_timeout = default_custom_attrs.pop('server_timeout', None)
    else:
        server_timeout = 300000

    if 'connect_timeout' in default_custom_attrs:
        connect_timeout = default_custom_attrs.pop('connect_timeout', None)
    else:
        connect_timeout = 5000

    conf = [
        'defaults',
        'log global',
        'retries 3',
        'option redispatch',
        'timeout connect %d' % connect_timeout,
        'timeout client %d' % client_timeout,
        'timeout server %d' % server_timeout,
    ]

    # Adding custom_attributes config
    for key, value in list(default_custom_attrs.items()):
        cmd = custom_attr_dict['default'][key]['cmd']
        conf.append(cmd % value)

    res = "\n\t".join(conf)
    return res

def set_v1_frontend_backend(pool, custom_attr_dict, custom_attrs):
    conf = []
    vip = VirtualIpSM.get(pool.virtual_ip)
    if not vip or not vip.params['admin_state']:
        return "\n"

    ssl = ''
    if vip.params['protocol'] == PROTO_HTTPS:
        ssl = 'ssl crt haproxy_ssl_cert_path no-sslv3'

    lconf = [
        'frontend %s' % vip.uuid,
        'option tcplog',
        'bind %s:%s %s' % (vip.params['address'],
            vip.params['protocol_port'], ssl),
        'mode %s' % PROTO_MAP_V1[vip.params['protocol']],
    ]

    if 'connection_limit' in vip.params and vip.params['connection_limit'] > 0:
         lconf.append('maxconn %d' % vip.params['connection_limit'])

    if vip.params['protocol'] == PROTO_HTTP or \
            vip.params['protocol'] == PROTO_HTTPS:
        lconf.append('option forwardfor')

    if pool and pool.params['admin_state']:
        frontend_custom_attrs = get_valid_attrs(custom_attr_dict, 'frontend',
                                                custom_attrs[pool.uuid])
        lconf.append('default_backend %s' % pool.uuid)
        # Adding custom_attributes config
        for key, value in list(frontend_custom_attrs.items()):
            cmd = custom_attr_dict['frontend'][key]['cmd']
            lconf.append(cmd % value)
        res = "\n\t".join(lconf) + '\n\n'
        mode = PROTO_MAP_V1[pool.params['protocol']]
        res += set_backend(pool, mode, custom_attr_dict, custom_attrs)
        conf.append(res)

    return "\n".join(conf)

def get_listeners(lb):
    listeners = []
    if lb.device_owner == 'K8S:LOADBALANCER':
        for ll_id in lb.loadbalancer_listeners:
            entry_found = False
            ll = LoadbalancerListenerSM.get(ll_id)
            if not ll:
               continue
            if not ll.params['admin_state']:
                continue
            port = ll.params['protocol_port']
            for listener_entry in listeners or []:
                if listener_entry['port'] == port:
                    entry_found = True
                    break
            if entry_found == True:
                sni_containers = listener_entry['sni_containers']
                sni_containers.extend(ll.params['sni_containers'])
                pools = listener_entry['pools']
                pools.append(ll.loadbalancer_pool)
            else:
                listener = {}
                listener['port'] = port
                listener['obj'] = ll
                listener['sni_containers'] = []
                if 'sni_containers' in ll.params:
                    listener['sni_containers'].extend(ll.params['sni_containers'])
                pools = []
                pools.append(ll.loadbalancer_pool)
                listener['pools'] = pools
                listeners.append(listener)
    else:
        for ll_id in lb.loadbalancer_listeners:
            ll = LoadbalancerListenerSM.get(ll_id)
            if not ll:
               continue
            if not ll.params['admin_state']:
                continue
            listener = {}
            listener['obj'] = ll
            listener['sni_containers'] = []
            if 'sni_containers' in ll.params:
                listener['sni_containers'].extend(ll.params['sni_containers'])
            pools = []
            pools.append(ll.loadbalancer_pool)
            listener['pools'] = pools
            listeners.append(listener)

    return listeners

def set_v2_frontend_backend(lb, custom_attr_dict, custom_attrs):
    conf = []
    lconf = ""
    pconf = ""

    listeners = get_listeners(lb)
    for listener in listeners:
        ll = listener['obj']
        sni_containers = listener['sni_containers']
        ssl = 'ssl'
        tls_sni_presence = False
        if ll.params['protocol'] == PROTO_TERMINATED_HTTPS:
            if ll.params['default_tls_container']:
                ssl += ' crt__%s' % ll.params['default_tls_container']
                tls_sni_presence = True
            for sni_container in sni_containers:
                ssl += ' crt__%s' % sni_container
                tls_sni_presence = True
        if (tls_sni_presence == False):
            ssl = ''
        else:
            ssl += ' no-sslv3'

        conf = [
            'frontend %s' % ll.uuid,
            'option tcplog',
            'bind %s:%s %s' % (lb.params['vip_address'],
                ll.params['protocol_port'], ssl),
            'mode %s' % PROTO_MAP_V2[ll.params['protocol']],
        ]

        if 'connection_limit' in ll.params and ll.params['connection_limit'] > 0:
            conf.append('maxconn %d' % ll.params['connection_limit'])

        if ll.params['protocol'] == PROTO_HTTP:
            conf.append('option forwardfor')

        pools = listener['pools']
        for pool_id in pools or []:
            pool =  LoadbalancerPoolSM.get(pool_id)
            if pool and pool.params['admin_state']:
                frontend_custom_attrs = get_valid_attrs(custom_attr_dict,
                                                        'frontend',
                                                        custom_attrs[pool.uuid])
                annotations = pool.annotations
                acl = {}
                if annotations and 'key_value_pair' in annotations:
                    for kv in annotations['key_value_pair'] or []:
                        acl[kv['key']] = kv['value']
                    if 'type' not in acl or acl['type'] == 'default':
                        conf.append('default_backend %s' % pool.uuid)
                    else:
                        acl_cdn = ""
                        host_cdn = ""
                        path_cdn = ""
                        if 'host' in acl:
                            host_cdn = "%s_host" % pool.uuid
                            conf.append('acl %s hdr(host) -i %s' %(host_cdn, acl['host']))
                        if 'path' in acl:
                            path_cdn = "%s_path" % pool.uuid
                            conf.append('acl %s_path path %s' %(pool.uuid, acl['path']))
                        acl_cdn = host_cdn + " " + path_cdn
                        conf.append('use_backend %s if %s' %(pool.uuid, acl_cdn))
                else:
                    conf.append('default_backend %s' % pool.uuid)

                # Adding custom_attributes config
                for key, value in list(frontend_custom_attrs.items()):
                    cmd = custom_attr_dict['frontend'][key]['cmd']
                    conf.append(cmd % value)
                conf.append("\n")
                mode = PROTO_MAP_V2[pool.params['protocol']]
                pconf += set_backend(pool, mode, custom_attr_dict, custom_attrs)
        lconf += "\n\t".join(conf)

    conf = []
    conf.append(lconf)
    conf.append(pconf)

    return "\n".join(conf)

def set_backend(pool, mode, custom_attr_dict, custom_attrs):
    backend_custom_attrs = get_valid_attrs(custom_attr_dict, 'backend',
                                           custom_attrs[pool.uuid])
    conf = [
        'backend %s' % pool.uuid,
        'mode %s' % mode,
        'balance %s' % LB_METHOD_MAP[pool.params['loadbalancer_method']]
    ]
    if pool.params['protocol'] == PROTO_HTTP:
        conf.append('option forwardfor')

    server_suffix = ''
    for hm_id in pool.loadbalancer_healthmonitors:
        hm = HealthMonitorSM.get(hm_id)
        if not hm:
            continue
        server_suffix, monitor_conf = set_health_monitor(hm)
        conf.extend(monitor_conf)

    session_conf_dict = get_session_persistence_conf(pool)
    persistence_type = session_conf_dict["type"]
    if persistence_type:
        conf.extend(session_conf_dict["conf"])

    for member_id in pool.members:
        member = LoadbalancerMemberSM.get(member_id)
        if not member or 'admin_state' not in member.params or \
            not member.params['admin_state']:
            continue
        server = (('server %s %s:%s weight %s') % (member.uuid,
                  member.params['address'], member.params['protocol_port'],
                  member.params['weight'])) + server_suffix
        if persistence_type and persistence_type == PERSISTENCE_HTTP_COOKIE:
            server += " cookie %s" % member.uuid
        conf.append(server)

    # Adding custom_attributes config
    for key, value in list(backend_custom_attrs.items()):
        cmd = custom_attr_dict['backend'][key]['cmd']
        conf.append(cmd % value)

    return "\n\t".join(conf) + "\n\n"

def set_health_monitor(hm):
    if 'admin_state' not in hm.params or not hm.params['admin_state']:
        return '', []

    server_suffix = ' check inter %ss fall %s' % \
        (hm.params['delay'], hm.params['max_retries'])
    conf = [
        'timeout check %ss' % hm.params['timeout']
    ]

    if hm.params['monitor_type'] in (HEALTH_MONITOR_HTTP, HEALTH_MONITOR_HTTPS):
        conf.append('option httpchk %s %s' %
            (hm.params['http_method'], hm.params['url_path']))
        conf.append(
            'http-check expect rstatus %s' %
            '|'.join(_get_codes(hm.params['expected_codes']))
        )

    if hm.params['monitor_type'] == HEALTH_MONITOR_HTTPS:
        conf.append('option ssl-hello-chk')

    return server_suffix, conf

def get_session_persistence_conf(pool):
    res = {'type': None, 'conf': []}
    if pool.virtual_ip:
        vip = VirtualIpSM.get(pool.virtual_ip)
        if not vip:
            return res
        persistence = vip.params.get('persistence_type', None)
        cookie = vip.params.get('persistence_cookie_name', None)
    else:
        persistence = pool.params.get('session_persistence', None)
        cookie = pool.params.get('persistence_cookie_name', None)

    if not persistence:
        return res

    res["type"] = persistence
    if persistence == PERSISTENCE_SOURCE_IP:
        res['conf'].append('stick-table type ip size 10k')
        res['conf'].append('stick on src')
    elif persistence == PERSISTENCE_HTTP_COOKIE:
        res['conf'].append('cookie SRV insert indirect nocache')
    elif (persistence == PERSISTENCE_APP_COOKIE and cookie):
        res['conf'].append('appsession %s len 56 timeout 3h' % cookie)
    return res

def _get_codes(codes):
    response = set()
    for code in codes.replace(',', ' ').split(' '):
        code = code.strip()
        if not code:
            continue
        elif '-' in code:
            low, hi = code.split('-')[:2]
            response.update(str(i) for i in range(int(low), int(hi) + 1))
        else:
            response.add(code)
    return response
