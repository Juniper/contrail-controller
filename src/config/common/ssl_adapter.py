# -*- coding: utf-8 -*-
# vim: tabstop=4 shiftwidth=4 softtabstop=4
# @author: Sanju Abraham, Juniper Networks, OpenContrail
from requests.adapters import HTTPAdapter
try:
    from urllib3.poolmanager import PoolManager
except ImportError:
    # This is required for redhat7 as it does not have
    # separate urllib3 package installed
    from requests.packages.urllib3.poolmanager import PoolManager
import ssl

class SSLAdapter(HTTPAdapter):
    '''An HTTPS Transport Adapter that can be configured with SSL/TLS version.'''
    def __init__(self, ssl_version=None, **kwargs):
        self.ssl_version = ssl_version

        super(SSLAdapter, self).__init__(**kwargs)

    def init_poolmanager(self, connections, maxsize, block=False):
        self.poolmanager = PoolManager(num_pools=connections,
                                       maxsize=maxsize,
                                       block=block,
                                       ssl_version=self.ssl_version)
