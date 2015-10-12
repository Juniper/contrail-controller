import logging
import inspect
from haproxy_cert import Barbican_Cert_Manager
import os

class CustomAttr(object):
    """This type handles non-flat data-types like
       int, str, bool.
    """
    def __init__(self, key, value):
        self._value = value
        self._key =  key

    def validate(self, conf_list):
        return

    def post_validation(self, conf_list):
        return

class CustomAttrTlsContainer(CustomAttr):
    def __init__(self, keystone_auth_conf_file, key, value):
        super(CustomAttrTlsContainer, self).__init__(key, value)
        self.cert_manager = Barbican_Cert_Manager(keystone_auth_conf_file)

    def validate(self):
        if self._key != 'tls_container':
            return False

        if (self.cert_manager and \
           self.cert_manager._validate_tls_secret(self._value)):
            tls_pem_string = self.cert_manager._populate_tls_pem(self._value)
            self._value = tls_pem_string
            return True
        else:
            logging.error("TLS container invalid")
            return False

    def post_validation(self):
        return self._value

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
        'tls_container': {
            'type': CustomAttrTlsContainer,
            'limits': None,
            'cmd': None
        }
    },
    'pool': {},
}

def validate_custom_attributes(config, section, keystone_auth_conf_file=None):
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
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                    elif type_attr == str:
                        if len(value) in range(limits[0], limits[1]):
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                    elif type_attr == bool:
                        if value in limits:
                            if value == 'True':
                                value = ''
                            elif value == 'False':
                                value = 'no '
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                    elif inspect.isclass(type_attr):
                        new_custom_attr = type_attr(keystone_auth_conf_file, \
                                                    key, value)
                        if new_custom_attr.validate():
                            value = new_custom_attr.post_validation()
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                except Exception as e:
                    logging.error(str(e))
                    continue

    return section_dict
