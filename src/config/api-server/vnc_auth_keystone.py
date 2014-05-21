#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
#
# This file contains authentication/authorization functionality for VNC-CFG
# subsystem. It also currently contains keystone adaptation which can in
# future by moved to vnc_auth_keystone.py
#

import gevent
from gevent import monkey
monkey.patch_all()
import ConfigParser
import bottle
import time

try:
    from keystoneclient.middleware import auth_token
except Exception:
    pass

# Open port for access to API server for trouble shooting


class AuthOpen(object):

    def __init__(self, host, port, app):
        self._http_host = host
        self._http_port = port
        self._http_app = bottle.Bottle()
        self._http_app.merge(app.routes)
        self._http_app.config.auth_open = True
    # end __init__

    def start_http_server(self):
        self._http_app.run(
            host=self._http_host, port=self._http_port, server='gevent')
    # end start_http_server
# end class AuthOpen

# Pre-auth filter


class AuthPreKeystone(object):

    def __init__(self, app, conf, multi_tenancy):
        self.app = app
        self.conf = conf
        self.mt = multi_tenancy

    def get_mt(self):
        return self.mt

    def set_mt(self, value):
        self.mt = value

    def __call__(self, env, start_response):
        app = self.app if self.mt else bottle.app()
        return app(env, start_response)

# Post-auth filter. Normalize user/role supplied by quantum plugin for
# consumption by Perms


class AuthPostKeystone(object):

    def __init__(self, app, conf):
        self.app = app
        self.conf = conf

    def __call__(self, env, start_response):
        # todo validate request is from quantum plugin
        # X-Api-User-id and X-Api-Role supplied by Quantum.
        # Note that Quantum sends admin token
        if 'HTTP_X_API_USER_ID' in env:
            env['HTTP_X_USER'] = self.conf[
                'auth_svc'].user_id_to_name(env['HTTP_X_API_USER_ID'])
        elif 'HTTP_X_API_USER' in env:
            env['HTTP_X_USER'] = env['HTTP_X_API_USER']

        if 'HTTP_X_API_ROLE' in env:
            env['HTTP_X_ROLE'] = env['HTTP_X_API_ROLE']
        return self.app(env, start_response)


class AuthServiceKeystone(object):

    def __init__(self, server_mgr, args):
        self._conf_info = {
            'auth_host': args.auth_host,
            'auth_port': args.auth_port,
            'auth_protocol': args.auth_protocol,
            'admin_user': args.admin_user,
            'admin_password': args.admin_password,
            'admin_tenant_name': args.admin_tenant_name,
        }
        self._server_mgr = server_mgr
        self._auth_method = args.auth
        self._multi_tenancy = args.multi_tenancy
        self._auth_token = None
        self._auth_middleware = None
        if not self._auth_method:
            return
        if self._auth_method != 'keystone':
            raise UnknownAuthMethod()

        # map keystone id to users. Needed for quantum plugin because contrail
        # plugin doesn't have access to user token and ends up sending admin
        # admin token along with user-id and role
        self._ks_users = {}

        # configure memcache if enabled
        if self._multi_tenancy and 'memcache_servers' in args:
            self._conf_info[
                'memcache_servers'] = args.memcache_servers.split(',')
            if 'token_cache_time' in args:
                self._conf_info['token_cache_time'] = args.token_cache_time
    # end __init__

    def json_request(self, method, path):
        if self._auth_token is None or self._auth_middleware is None:
            return {}
        headers = {'X-Auth-Token': self._auth_token}
        response, data = self._auth_middleware._json_request(
            method, path, additional_headers=headers)
        try:
            status_code = response.status_code
        except AttributeError:
            status_code = response.status

        return data if status_code == 200 else {}
    # end json_request

    def get_projects(self):
        return self.json_request('GET', '/v2.0/tenants')
    # end get_projects

    def get_middleware_app(self):
        if not self._auth_method:
            return None

        if not self._multi_tenancy:
            return None

        # keystone middleware is needed for fetching objects

        # app = bottle.app()
        app = AuthPostKeystone(bottle.app(), {'auth_svc': self})

        auth_middleware = auth_token.AuthProtocol(app, self._conf_info)
        self._auth_middleware = auth_middleware
        while True:
            try:
                self._auth_token = auth_middleware.get_admin_token()
                break
            except auth_token.ServiceError as e:
                self._server_mgr.config_log_error(
                    "Error in getting admin token: " + str(e))
                time.sleep(2)

        # open access for troubleshooting
        self._open_app = AuthOpen('127.0.0.1', '8095', bottle.app())
        gevent.spawn(self._open_app.start_http_server)

        app = auth_middleware

        # allow multi tenancy to be updated dynamically
        app = AuthPreKeystone(
            auth_middleware,
            {'admin_token': self._auth_token},
            self._multi_tenancy)

        return app
    # end get_middleware_app

    def verify_signed_token(self, user_token):
        try:
            return self._auth_middleware.verify_signed_token(user_token)
        except:
            return None
    # end

    # convert keystone user id to name
    def user_id_to_name(self, id):
        if id in self._ks_users:
            return self._ks_users[id]

        # fetch from keystone
        content = self.json_request('GET', '/v2.0/users')
        if 'users' in content:
            self._ks_users = dict((user['id'], user['name'])
                                  for user in content['users'])

        # check it again
        if id in self._ks_users:
            return self._ks_users[id]
        else:
            return ''
    # end user_id_to_name
# end class AuthService
