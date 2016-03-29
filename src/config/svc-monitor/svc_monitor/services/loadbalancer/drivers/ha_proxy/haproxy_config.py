from svc_monitor.config_db import *

PROTO_HTTP = 'HTTP'
PROTO_HTTPS = 'HTTPS'

PROTO_MAP = {
    'TCP': 'tcp',
    'HTTP': 'http',
    'HTTPS': 'http',
    'TERMINATED_HTTPS': 'terminated_https'
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
    sock_path = '/var/lib/contrail/loadbalancer/'
    sock_path += lb.uuid + '/haproxy.sock'
    conf = set_globals(sock_path) + '\n\n'
    conf += set_defaults() + '\n\n'
    conf += set_v2_frontend_backend(lb)
    return conf

def get_config_v1(pool):
    sock_path = '/var/lib/contrail/loadbalancer/'
    sock_path += pool.uuid + '/haproxy.sock'
    conf = set_globals(sock_path) + '\n\n'
    conf += set_defaults() + '\n\n'
    conf += set_v1_frontend_backend(pool)
    return conf

def set_globals(sock_path):
    maxconn = 65000
    ssl_ciphers = \
        'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:' \
        'ECDH+AES128:DH+AES:ECDH+3DES:DH+3DES:RSA+AESGCM:' \
        'RSA+AES:RSA+3DES:!aNULL:!MD5:!DSS'

    conf = [
        'global',
        'daemon',
        'user nobody',
        'group nogroup',
        'log /dev/log local0',
        'log /dev/log local1 notice',
        'tune.ssl.default-dh-param 2048',
        'ssl-default-bind-ciphers %s' % ssl_ciphers,
        'ulimit-n 200000',
        'maxconn %d' % maxconn
    ]
    conf.append('stats socket %s mode 0666 level user' % sock_path)
    res = "\n\t".join(conf)
    return res

def set_defaults():
    client_timeout = 300000
    server_timeout = 300000
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

    res = "\n\t".join(conf)
    return res

def set_v1_frontend_backend(pool):
    conf = []
    vip = VirtualIpSM.get(pool.virtual_ip)
    if not vip and not vip.params['admin_state']:
        return

    ssl = ''
    if vip.params['protocol'] == PROTO_HTTPS:
        ssl = 'ssl crt %s no-sslv3' % crt_file

    lconf = [
        'frontend %s' % vip.uuid,
        'option tcplog',
        'bind %s:%s %s' % (vip.params['address'],
            vip.params['protocol_port'], ssl),
        'mode %s' % PROTO_MAP[vip.params['protocol']]
    ]
    if vip.params['protocol'] == PROTO_HTTP or \
            vip.params['protocol'] == PROTO_HTTPS:
        lconf.append('option forwardfor')

    if pool and pool.params['admin_state']:
        lconf.append('default_backend %s' % pool.uuid)
        res = "\n\t".join(lconf) + '\n\n'
        res += set_backend(pool)
        conf.append(res)

    return "\n".join(conf)

def set_v2_frontend_backend(lb):
    conf = []
    for ll_id in lb.loadbalancer_listeners:
        ll = LoadbalancerListenerSM.get(ll_id)
        if not ll:
            continue
        if not ll.params['admin_state']:
            continue

        ssl = ''
        if ll.params['protocol'] == PROTO_HTTPS:
            ssl = 'ssl crt %s no-sslv3' % crt_file

        lconf = [
            'frontend %s' % ll.uuid,
            'option tcplog',
            'bind %s:%s %s' % (lb.params['vip_address'],
                ll.params['protocol_port'], ssl),
            'mode %s' % PROTO_MAP[ll.params['protocol']]
        ]
        if ll.params['protocol'] == PROTO_HTTP or \
                ll.params['protocol'] == PROTO_HTTPS:
            lconf.append('option forwardfor')

        pool =  LoadbalancerPoolSM.get(ll.loadbalancer_pool)
        if pool and pool.params['admin_state']:
            lconf.append('default_backend %s' % pool.uuid)
            res = "\n\t".join(lconf) + '\n\n'
            res += set_backend(pool)
            conf.append(res)

    return "\n".join(conf)

def set_backend(pool):
    conf = [
        'backend %s' % pool.uuid,
        'mode %s' % PROTO_MAP[pool.params['protocol']],
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

    session_conf = set_session_persistence(pool)
    conf.extend(session_conf)

    for member_id in pool.members:
        member = LoadbalancerMemberSM.get(member_id)
        if not member or not member.params['admin_state']:
            continue
        server = (('server %s %s:%s weight %s') % (member.uuid,
                  member.params['address'], member.params['protocol_port'],
                  member.params['weight'])) + server_suffix
        conf.append(server)

    return "\n\t".join(conf) + '\n'

def set_health_monitor(hm):
    if not hm.params['admin_state']:
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

def set_session_persistence(pool):
    conf = []
    if pool.virtual_ip:
        vip = VirtualIpSM.get(pool.virtual_ip)
        if not vip:
            return
        persistence = vip.params.get('persistence_type', None)
        cookie = vip.params.get('persistence_cookie_name', None)
    else:
        persistence = pool.params.get('session_persistence', None)
        cookie = pool.params.get('persistence_cookie_name', None)

    if persistence == PERSISTENCE_SOURCE_IP:
        conf.append('stick-table type ip size 10k')
        conf.append('stick on src')
    elif persistence == PERSISTENCE_HTTP_COOKIE:
        conf.append('cookie SRV insert indirect nocache')
    elif (persistence == PERSISTENCE_APP_COOKIE and cookie):
        conf.append('appsession %s len 56 timeout 3h' % cookie)
    return conf

def _get_codes(codes):
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
