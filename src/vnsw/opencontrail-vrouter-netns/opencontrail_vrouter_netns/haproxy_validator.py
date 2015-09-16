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
        }
    },
    'pool': {},
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
                except Exception as e:
                    print "Skipping key: %s, value: %s due to validation failure" \
                        % (key, value)
                    continue

    return section_dict
