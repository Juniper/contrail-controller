import json
import os
import logging

def validate_custom_attributes(config, section, keystone_auth_conf_file=None):
    return {}

try:
    from haproxy_validator import validate_custom_attributes as validator
    from haproxy_validator import custom_attributes_dict
    from haproxy_cert import Barbican_Cert_Manager
except ImportError:
    validator = validate_custom_attributes
    custom_attributes_dict = {}

# Setup logger
logging.basicConfig(filename='/var/log/contrail/haproxy_parse.log', level=logging.WARNING)

# Setup global definitions
PROTO_TCP = 'TCP'
PROTO_HTTP = 'HTTP'
PROTO_HTTPS = 'HTTPS'

PROTO_MAP = {
    PROTO_TCP: 'tcp',
    PROTO_HTTP: 'http',
    PROTO_HTTPS: 'http'
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

HTTPS_PORT = 443

def build_config(conf_file, keystone_auth_conf_file):
    with open(conf_file) as data_file:
        config = json.load(data_file)
    conf_dir = os.path.dirname(conf_file)

    conf = []
    sock_path = conf_dir + '/haproxy.sock'
    conf = _set_global_config(config, sock_path) + '\n\n'
    conf += _set_defaults(config) + '\n\n'
    conf += _set_frontend(config, conf_dir, keystone_auth_conf_file) + '\n\n'
    conf += _set_backend(config) + '\n'
    filename = conf_dir + '/haproxy.conf'
    conf_file = open(filename, 'w')
    conf_file.write(conf)
    return filename

def _construct_config_block(lb_config, conf, custom_attr_section, custom_attributes):
    for key, value in custom_attributes.iteritems():
        cmd = custom_attributes_dict[custom_attr_section][key]['cmd']
        conf.append(cmd % value)

    res = "\n\t".join(conf)
    return res

def _set_global_config(config, sock_path):
    global_custom_attributes = validator(config, 'global')
    maxconn = global_custom_attributes.pop('max_conn', None) \
        if 'max_conn' in global_custom_attributes else 65000
    ssl_ciphers = global_custom_attributes.pop('ssl_ciphers', None) \
        if 'ssl_ciphers' in global_custom_attributes else \
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

    return _construct_config_block(config, conf, "global", global_custom_attributes)


def _set_defaults(config):
    default_custom_attributes = validator(config, 'default')
    client_timeout = default_custom_attributes.pop('client_timeout', None) \
        if 'client_timeout' in default_custom_attributes else 300000
    server_timeout = default_custom_attributes.pop('server_timeout', None) \
        if 'server_timeout' in default_custom_attributes else 300000
    connect_timeout = default_custom_attributes.pop('connect_timeout', None) \
        if 'connect_timeout' in default_custom_attributes else 5000

    conf = [
        'defaults',
        'log global',
        'retries 3',
        'option redispatch',
        'timeout connect %d' % connect_timeout,
        'timeout client %d' % client_timeout,
        'timeout server %d' % server_timeout,
    ]

    return _construct_config_block(config, conf, "default", default_custom_attributes)

def _set_frontend_v2(config, conf_dir, keystone_auth_conf_file):
    conf = []
    for listener in config['listeners']:
        if not listener['admin-state']:
            continue
        lconf = [
            'frontend %s' % listener['id'],
            'option tcplog',
            'bind %s:%d' % (config['loadbalancer']['vip-address'],
                            listener['port']),
            'mode %s' % PROTO_MAP[listener['protocol']]
        ]
        if listener['pools'] and listener['pools'][0]['pool']['admin-state']:
            lconf.append('default_backend %s'
                         % listener['pools'][0]['pool']['id'])
        res = "\n\t".join(lconf)
        conf.append(res)
    return "\n".join(conf)

def _set_frontend(config, conf_dir, keystone_auth_conf_file):
    if 'loadbalancer' in config:
        return _set_frontend_v2(config, conf_dir, keystone_auth_conf_file)

    port = config['vip']['port']
    vip_custom_attributes = validator(config, 'vip', keystone_auth_conf_file)
    ssl = ''

    if 'tls_container' in vip_custom_attributes:
        data = vip_custom_attributes.pop('tls_container', None)
        crt_file = _populate_pem_file(data, conf_dir)
    else:
        crt_file = config['ssl-crt']

    if config['vip']['protocol'] == PROTO_HTTPS:
        ssl = 'ssl crt %s no-sslv3' % crt_file
    conf = [
        'frontend %s' % config['vip']['id'],
        'option tcplog',
        'bind %s:%d %s' % (config['vip']['address'], port, ssl),
        'mode %s' % PROTO_MAP[config['vip']['protocol']],
        'default_backend %s' % config['pool']['id']
    ]
    if config['vip']['connection-limit'] >= 0:
        conf.append('maxconn %s' % config['vip']['connection-limit'])
    if config['vip']['protocol'] == PROTO_HTTP or \
            config['vip']['protocol'] == PROTO_HTTPS:
        conf.append('option forwardfor')

    return _construct_config_block(config, conf, "vip", vip_custom_attributes)

def _set_backend_v2(config):
    conf = []
    for listener in config['listeners']:
        if not listener['pools'] or not listener['admin-state']:
            continue
        pool = listener['pools'][0]['pool']
        if not pool['admin-state']:
            continue
        lconf = [
            'backend %s' % pool['id'],
            'mode %s' % PROTO_MAP[pool['protocol']],
            'balance %s' % LB_METHOD_MAP[pool['method']]
        ]
        if pool['protocol'] == PROTO_HTTP:
            lconf.append('option forwardfor')

        for member in listener['pools'][0]['members']:
            if not member['admin-state']:
                continue
            server = (('server %(id)s %(address)s:%(port)s '
                      'weight %(weight)s') % member) + ''
            lconf.append(server)
        res = "\n\t".join(lconf)
        conf.append(res)
    return "\n".join(conf)

def _set_backend(config):
    if 'loadbalancer' in config:
        return _set_backend_v2(config)

    pool_custom_attributes = validator(config, 'pool')
    conf = [
        'backend %s' % config['pool']['id'],
        'mode %s' % PROTO_MAP[config['pool']['protocol']],
        'balance %s' % LB_METHOD_MAP[config['pool']['method']]
    ]
    if config['pool']['protocol'] == PROTO_HTTP:
        conf.append('option forwardfor')

    server_suffix, monitor_conf = _set_health_monitor(config)
    conf.extend(monitor_conf)
    session_conf = _set_session_persistence(config)
    conf.extend(session_conf)

    for member in config['members']:
        if not member['admin-state']:
            continue
        server = (('server %(id)s %(address)s:%(port)s '
                  'weight %(weight)s') % member) + server_suffix
        if (config['vip']['persistence-type'] == PERSISTENCE_HTTP_COOKIE):
            server += ' cookie %d' % config['members'].index(member)
        conf.append(server)

    return _construct_config_block(config, conf, "pool", pool_custom_attributes)

def _set_health_monitor(config):
    for monitor in config['healthmonitors']:
        if monitor['admin-state']:
            break
    else:
        return '', []

    server_suffix = ' check inter %(delay)ds fall %(max-retries)d' % monitor
    conf = [
        'timeout check %ds' % monitor['timeout']
    ]

    if monitor['type'] in (HEALTH_MONITOR_HTTP, HEALTH_MONITOR_HTTPS):
        conf.append('option httpchk %(http-method)s %(url)s' % monitor)
        conf.append(
            'http-check expect rstatus %s' %
            '|'.join(_get_codes(monitor['expected-codes']))
        )

    if monitor['type'] == HEALTH_MONITOR_HTTPS:
        conf.append('option ssl-hello-chk')

    return server_suffix, conf

def _set_session_persistence(config):
    conf = []
    persistence = config['vip']['persistence-type']
    cookie = config['vip']['persistence-cookie-name']
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

def _populate_pem_file(data, conf_dir):
    crt_filename = conf_dir + '/crtbundle.pem'
    with open(crt_filename, 'w+') as outfile:
        outfile.write(data)

    return crt_filename
