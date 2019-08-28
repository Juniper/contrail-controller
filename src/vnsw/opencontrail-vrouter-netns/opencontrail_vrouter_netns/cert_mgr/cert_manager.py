from __future__ import absolute_import
from builtins import object
import logging
import exceptions

try:
    from .barbican_cert_manager import BarbicanCertManager
except ImportError:
    pass

try:
    from .kubernetes_cert import KubernetesCert
except ImportError:
    pass

class CertManager(object):

    def __init__(self):
        pass

    @staticmethod
    def update_ssl_config(orchestrator, provider, \
        haproxy_config, auth_conf, dest_dir):
        if orchestrator == 'openstack':
            if provider == 'barbican':
                updated_config = (BarbicanCertManager(auth_conf)). \
                    update_ssl_config(haproxy_config, dest_dir)
        elif orchestrator == 'kubernetes':
            updated_config = (KubernetesCert(auth_conf)). \
                update_ssl_config(haproxy_config, dest_dir)
        return updated_config
