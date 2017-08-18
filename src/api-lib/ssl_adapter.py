""" HTTPS Transport Adapter for python-requests, that allows configuration of
    SSL version"""
# -*- coding: utf-8 -*-
# vim: tabstop=4 shiftwidth=4 softtabstop=4
# @author: Sanju Abraham, Juniper Networks, OpenContrail
from requests.adapters import HTTPAdapter
try:
    # This is required for RDO, which installs both python-requests
    # and python-urllib3, but symlinks python-request's internally packaged
    # urllib3 to the site installed one.
    from requests.packages.urllib3.poolmanager import PoolManager
except ImportError:
    # Fallback to standard installation methods
    from urllib3.poolmanager import PoolManager


class SSLAdapter(HTTPAdapter):
    '''An HTTPS Transport Adapter that can be configured with SSL/TLS
       version.'''
    HTTPAdapter.__attrs__.extend(['ssl_version'])

    def __init__(self, ssl_version=None, **kwargs):
        self.ssl_version = ssl_version
        self.poolmanager = None

        super(SSLAdapter, self).__init__(**kwargs)

    def init_poolmanager(self, connections, maxsize, block=False):
        self.poolmanager = PoolManager(num_pools=connections,
                                       maxsize=maxsize,
                                       block=block,
                                       ssl_version=self.ssl_version)
