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

def build_config(config, conf_dir):
    conf = []
    sock_path = conf_dir + 'sock'
    conf = _set_global_config(config, sock_path) + '\n\n'
    conf += _set_defaults(config) + '\n\n'
    conf += _set_frontend(config) + '\n\n'
    conf += _set_backend(config) + '\n'
    print conf
    filename = conf_dir + 'conf'
    conf_file = open(filename, 'w')
    conf_file.write(conf)

def _set_global_config(config, sock_path):
    conf = [
        'global',
        'daemon',
        'user nobody',
        'group nogroup'
        'log /dev/log local0',
        'log /dev/log local1 notice'
    ]
    conf.append('stats socket %s mode 0666 level user' % sock_path)
    return ("\n\t".join(conf))

def _set_defaults(config):
    conf = [
        'defaults',
        'log global',
        'retries 3',
        'option redispatch',
        'timeout connect 5000',
        'timeout client 50000',
        'timeout server 50000',
    ]
    return ("\n\t".join(conf))

def _set_frontend(config):
    port = config['vip']['port']
    ssl = ''
    if config['vip']['protocol'] == PROTO_HTTPS:
        ssl = 'ssl crt %s' % config['ssl-crt']
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
    return ("\n\t".join(conf))

def _set_backend(config):
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

    return ("\n\t".join(conf))

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
