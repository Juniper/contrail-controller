#!/usr/bin/python
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.vnc_api import VncApi


class VncApiAdmin(VncApi):
    """ Api client library which connects to admin port of api-server.
    """
    def __init__(self, use_admin_api, username=None, password=None,
                 tenant_name=None, api_server_host='127.0.0.1',
                 api_server_port=8095, api_server_url='/',
                 api_server_use_ssl=False, auth_host=None):
        self.api_server_host = api_server_host
        self.api_server_port = api_server_port
        self.api_server_use_ssl = api_server_use_ssl
        self.auth_host = auth_host
        self.use_admin_api = use_admin_api
        if self.use_admin_api:
            self.api_server_host = '127.0.0.1'
            self.api_server_port = 8095
            self.api_server_use_ssl = False
            self.auth_host = None

        super(VncApiAdmin, self).__init__(
                username=username,
                password=password,
                tenant_name=tenant_name,
                api_server_host=self.api_server_host,
                api_server_port=self.api_server_port,
                api_server_url=api_server_url,
                api_server_use_ssl=self.api_server_use_ssl,
                auth_host=self.auth_host)

    def _authenticate(self, response=None, headers=None):
        if self.use_admin_api:
            sessions = self._api_server_session.api_server_sessions
            for host, session in sessions.items():
                session.auth = (self._username, self._password)
                sessions.update({host: session})
            return headers
        else:
            return super(VncApiAdmin, self)._authenticate(response, headers)
