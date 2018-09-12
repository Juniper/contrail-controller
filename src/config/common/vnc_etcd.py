#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
import etcd3


class VncEtcdClient(object):
    def __init__(self, host, port, logger, credential, ssl_enabled, ca_certs):
        self._host = host
        self._port = port
        self._logger = logger
        self._credential = credential
        self._ssl_enabled = ssl_enabled
        self._ca_certs = ca_certs

        self._client = self._new_client()

    def __getattr__(self, name):
        return getattr(self._client, name)

    def _new_client(self):
        kwargs = {'host': self._host, 'port': self._port}

        if self._credential:
            kwargs['user'] = self._credential['username']
            kwargs['password'] = self._credential['password']

        if self._ssl_enabled:
            kwargs['ca_cert'] = self._credential['ca_cert']
            kwargs['cert_key'] = self._credential['ca_key']

        return etcd3.Etcd3Client(**kwargs)
