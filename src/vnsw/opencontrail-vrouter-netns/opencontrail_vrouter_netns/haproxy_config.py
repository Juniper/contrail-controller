import json
import os
from ConfigParser import ConfigParser

PROTO_TCP = 'TCP'
PROTO_HTTP = 'HTTP'
PROTO_HTTPS = 'HTTPS'

PROTO_MAP = {
    PROTO_TCP: 'tcp',
    PROTO_HTTP: 'http',
    PROTO_HTTPS: 'tcp'
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


def build_config(conf_file):
    with open(conf_file) as data_file:
        config = json.load(data_file)
    conf_dir = os.path.dirname(conf_file)

    conf = []
    sock_path = conf_dir + '/haproxy.sock'
    externals = None
    ext_sections = []
    if 'lb-cfg' in config and os.path.exists(config['lb-cfg']):
        externals = ConfigParser()
        externals.read(config['lb-cfg'])
        ext_sections = set(externals.sections())

    ext = lambda x: dict(externals.items(x)) if (externals and (
        x in ext_sections and not ext_sections.discard(x))) else {}
    conf = _set_global_config(config, sock_path, ext('global')) + '\n\n'
    conf += _set_defaults(config, ext('defaults')) + '\n\n'
    conf += _set_frontend(config, ext('frontend')) + '\n\n'
    conf += _set_backend(config, ext('backend')) + '\n\n'

    print " -- Ext sections %s" % ext_sections
    for section in ext_sections:
        conf += _frame_haproxy_config_section(
            section, {}, dict(externals.items(section))) + '\n\n'

    filename = conf_dir + '/haproxy.conf'
    conf_file = open(filename, 'w')
    conf_file.write(conf)
    return filename


def _frame_haproxy_config_section(name, defaults, externals):
    if externals:
        defaults.update(externals)

    return '\n\t'.join([name] + [" ".join([k, v])
        for k, v, in defaults.iteritems()])


def _set_global_config(config, sock_path, externals):
    conf = {
        'daemon': '',
        'user': 'nobody',
        'group': 'nogroup',
        'log /dev/log local0': '',
        'log /dev/log local1': 'notice',
        'stats socket': '%s mode 0666 level user' % sock_path
    }
    return _frame_haproxy_config_section('global', conf, externals)


def _set_defaults(config, externals):
    conf = {
        'log': 'global',
        'retries': '3',
        'option': 'redispatch',
        'timeout connect': '5000',
        'timeout client': '50000',
        'timeout server': '50000',
    }
    return _frame_haproxy_config_section('defaults', conf, externals)


def _set_frontend(config, externals):
    port = config['vip']['port']
    ssl = ''
    if port == HTTPS_PORT:
        ssl = 'ssl crt %s' % ssl_cert_path
    conf = {
        'option': 'tcplog',
        'bind': '%s:%d %s' % (config['vip']['address'], port, ssl),
        'mode': '%s' % PROTO_MAP[config['vip']['protocol']],
        'default_backend': '%s' % config['pool']['id']
    }
    if config['vip']['connection-limit'] >= 0:
        conf['maxconn'] = '%s' % config['vip']['connection-limit']
    if config['vip']['protocol'] == PROTO_HTTP:
        conf['option forwardfor'] = ''

    for key in ['bind', 'mode', 'default_backend', 'maxconn']:
        # we don't want these from external as these are configured by client
        externals.pop(key, None)

    return _frame_haproxy_config_section(
        'frontend %s' % config['vip']['id'], conf, externals)


def _set_backend(config, externals):
    print " -- Externals = " + str(externals)
    conf = {
        'mode': '%s' % PROTO_MAP[config['pool']['protocol']],
        'balance': '%s' % LB_METHOD_MAP[config['pool']['method']]
    }
    if config['pool']['protocol'] == PROTO_HTTP:
        conf['option forwardfor'] = ''

    server_suffix, monitor_conf = _set_health_monitor(config)
    conf.update(monitor_conf)
    session_conf = _set_session_persistence(config)
    conf.update(session_conf)

    for member in config['members']:
        if not member['admin-state']:
            continue
        server = (('%(id)s %(address)s:%(port)s '
                  'weight %(weight)s') % member) + server_suffix
        if (config['vip']['persistence-type'] == PERSISTENCE_HTTP_COOKIE):
            server += ' cookie %d' % config['members'].index(member)
        conf['server'] = server

    for cfg in ['mode', 'balance', 'server', 'cookie', 'stick-table',
                'stick on', 'appsession']:
        # we don't want these from external as these are configured by client
        externals.pop(cfg, None)

    return _frame_haproxy_config_section(
        'backend %s' % config['pool']['id'], conf, externals)


def _set_health_monitor(config):
    for monitor in config['healthmonitors']:
        if monitor['admin-state']:
            break
    else:
        return '', []

    server_suffix = ' check inter %(delay)ds fall %(max-retries)d' % monitor
    conf = {
        'timeout check': '%ds' % monitor['timeout']
    }

    if monitor['type'] in (HEALTH_MONITOR_HTTP, HEALTH_MONITOR_HTTPS):
        conf['option httpchk'] = '%(http-method)s %(url)s' % monitor
        conf['http-check expect rstatus'] = (
            '%s' % '|'.join(_get_codes(monitor['expected-codes']))
        )

    if monitor['type'] == HEALTH_MONITOR_HTTPS:
        conf['option ssl-hello-chk']

    return server_suffix, conf


def _set_session_persistence(config):
    conf = dict()
    persistence = config['vip']['persistence-type']
    cookie = config['vip']['persistence-cookie-name']
    if persistence == PERSISTENCE_SOURCE_IP:
        conf['stick-table'] = 'type ip size 10k'
        conf['stick on'] = 'src'
    elif persistence == PERSISTENCE_HTTP_COOKIE:
        conf['cookie'] = 'SRV insert indirect nocache'
    elif (persistence == PERSISTENCE_APP_COOKIE and cookie):
        conf['appsession'] = '%s len 56 timeout 3h' % cookie
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
