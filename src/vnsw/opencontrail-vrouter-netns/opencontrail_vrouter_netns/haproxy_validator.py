import inspect
import re
import shlex


class CustomAttr(object):
    # This type meant to do more things than just a flat data type
    # pre()
    #    This method will be called with the loadbalancer config and
    #    list of config items that the haproxy_config has used
    #    before framing the haproxy config section and it returns None
    # post()
    #    This method is called after the configuration block is framed
    #    If this methid returns a string, it gets appended to the existing
    #    block
    def __init__(self, value):
        self._value = value

    def pre(self, conf_list):
        return

    def post(self):
        return


class CustomAttrTcpMultiPortBinding(CustomAttr):
    SUFFIX = "-tcp-multi-port"
    def __init__(self, value):
        super(CustomAttrTcpMultiPortBinding, self).__init__(value)
        self._port_map = {}
        self._port = None
        self._is_tcp = None
        self._conf_list = []
        self._parse()

    def _parse(self):
        tuples = shlex.split(self._value)
        for t in tuples:
            fr, to = t.split("/")
            self._port_map[fr] = to

    def post(self):
        # return if not tcp
        if not self._is_tcp:
            return

        # return if this port is not configured
        if self._port not in self._port_map:
            return

        return "\n" + "\n\t".join(self._conf_list)


class CustomAttrTcpMultiPortBindingFrontend(CustomAttrTcpMultiPortBinding):
    def __init__(self, value):
        super(CustomAttrTcpMultiPortBindingFrontend, self).__init__(value)

    def pre(self, lb_config, conf_list):
        for item in conf_list:
            if item.startswith("bind"):
                exp = re.compile("bind\s+(?P<bind_address>.*?):(?P<port>\d+)\s*(?P<remaining>.*)")
                matcher = exp.search(item)
                if not matcher:
                    # Not in the expected format
                    continue
                else:
                    match = matcher.groupdict()
                    self._port = match.get("port")
                    if self._port in self._port_map:
                        self._conf_list.append("bind %s:%s %s" % (
                            match.get("bind_address"),
                            self._port_map[self._port],
                            match.get("remaining", "")))
            elif item.startswith("mode"):
                _, mode = shlex.split(item)
                self._is_tcp = (mode == 'tcp')
                if self._is_tcp:
                    self._conf_list.append(item)
            elif item.startswith("default_backend"):
                _, id = shlex.split(item)
                self._conf_list.append("default_backend %s%s" % (id, self.SUFFIX))
            elif item.startswith("frontend"):
                _, id = shlex.split(item)
                self._conf_list.append("frontend %s%s" % (id, self.SUFFIX))
            else:
                self._conf_list.append(item)


class CustomAttrTcpMultiPortBindingBackend(CustomAttrTcpMultiPortBinding):
    def __init__(self, value):
        super(CustomAttrTcpMultiPortBindingBackend, self).__init__(value)

    def pre(self, lb_config, config_list):
        self._port = str(lb_config['vip']['port'])
        self._is_tcp = lb_config['vip']['protocol'] == 'TCP'
        exp = re.compile(
            "server\s+(?P<server_id>\S+)\s+(?P<bind_address>.*?)"\
            ":(?P<port>\d+)(?P<remaining>.*)")

        for item in config_list:
            if item.startswith("backend"):
                _, id = shlex.split(item)
                self._conf_list.append("backend %s%s" % (id, self.SUFFIX))
            elif item.startswith('server'):
                matcher = exp.search(item)
                if self._port in self._port_map and matcher:
                    match = matcher.groupdict()
                    self._conf_list.append("server %s %s:%s %s" % (
                        match.get("server_id"),
                        match.get("bind_address"),
                        self._port_map[self._port],
                        match.get("remaining")))
            else:
                self._conf_list.append(item)



custom_attributes_dict = {
    'global': {
        'max_conn': {
            'type': int,
            'limits': [1, 65535],
            'cmd': 'maxconn %d'
        },
        'max_conn_rate': {
            'type': int,
            'limits': [1, 65535],
            'cmd': 'maxconnrate %d'
        },
        'max_sess_rate': {
            'type': int,
            'limits': [1, 65535],
            'cmd': 'maxsessrate %d'
        },
        'max_ssl_conn': {
            'type': int,
            'limits': [1, 65535],
            'cmd': 'maxsslconn %d'
        },
        'max_ssl_rate': {
            'type': int,
            'limits': [1, 65535],
            'cmd': 'maxsslrate %d'
        },
        'ssl_ciphers': {
            'type': str,
            'limits': [1, 100],
            'cmd': 'ssl-default-bind-ciphers %s'
        },
        'tune_http_max_header': {
            'type': int,
            'limits': [1, 128],
            'cmd': 'tune.http.maxhdr %d'
        },
        'tune_ssl_max_record': {
            'type': int,
            'limits': [1, 16384],
            'cmd': 'tune.ssl.maxrecord %d'
        }
    },
    'default': {
        'server_timeout': {
            'type': int,
            'limits': [1, 5000000],
             'cmd': 'timeout server %d'
        },
        'client_timeout': {
            'type': int,
            'limits': [1, 5000000],
            'cmd': 'timeout client %d'
        },
        'connect_timeout': {
            'type': int,
            'limits': [1, 5000000],
            'cmd': 'timeout connect %d'
        }
    },
    'vip': {
        'http_server_close': {
            'type': bool,
            'limits': ['True', 'False'],
            'cmd': '%soption http-server-close'
        },
        'rate_limit_sessions': {
            'type': int,
            'limits': [1, 65535],
            'cmd': 'rate-limit sessions %d'
        },
        'tcp_multi_port_binding': {
            'type': CustomAttrTcpMultiPortBindingFrontend,
            'limits': None
        }
    },
    'pool': {
        'tcp_multi_port_binding': {
            'type': CustomAttrTcpMultiPortBindingBackend,
            'limits': None
        }
    },
}

def validate_custom_attributes(config, section):
    section_dict = {}
    if 'custom-attributes' in config and section in custom_attributes_dict:
        custom_attributes = config['custom-attributes']
        for key, value in custom_attributes.iteritems():
            if key in custom_attributes_dict[section]:
                #Sanitize the value
                try:
                    type_attr = custom_attributes_dict[section][key]['type']
                    limits = custom_attributes_dict[section][key]['limits']
                    if type_attr == int:
                        value = type_attr(value)
                        if value in range(limits[0], limits[1]):
                            section_dict.update({key:value})
                    elif type_attr == str:
                        if len(value) in range(limits[0], limits[1]):
                            section_dict.update({key:value})
                    elif type_attr == bool:
                        if value in limits:
                            if value == 'True':
                                value = ''
                            elif value == 'False':
                                value = 'no '
                            section_dict.update({key:value})
                    elif inspect.isclass(type_attr):
                        section_dict.update({key: type_attr(value)})
                except Exception as e:
                    print "Skipping key: %s, value: %s due to validation failure" \
                        % (key, value)
                    continue
    return section_dict
