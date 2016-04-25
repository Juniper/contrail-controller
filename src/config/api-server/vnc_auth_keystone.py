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
import base64

try:
    from keystoneclient.middleware import auth_token
except ImportError:
    try:
        from keystonemiddleware import auth_token
    except ImportError:
        pass

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_bottle import get_bottle_server
from cfgm_common import utils as cfgmutils

#keystone SSL cert bundle
_DEFAULT_KS_CERT_BUNDLE="/tmp/keystonecertbundle.pem"

# Open port for access to API server for trouble shooting
class LocalAuth(object):

    def __init__(self, app, conf_info):
        self._http_host = 'localhost'
        self._http_port = conf_info['admin_port']
        self._http_app = bottle.Bottle()
        self._http_app.merge(app.routes)
        self._http_app.config.local_auth = True
        self._http_app.error_handler = app.error_handler
        self._conf_info = conf_info

        # 2 decorators below due to change in api between bottle 0.11.6
        # (which insists on global app) vs later (which need on specific
        # app)
        @self._http_app.hook('before_request')
        @bottle.hook('before_request')
        def local_auth_check(*args, **kwargs):
            if bottle.request.app != self._http_app:
                return
            # expect header to have something like 'Basic YWJjOmRlZg=='
            auth_hdr_val = bottle.request.environ.get('HTTP_AUTHORIZATION')
            if not auth_hdr_val:
                bottle.abort(401, 'HTTP_AUTHORIZATION header missing')
            try:
                auth_type, user_passwd = auth_hdr_val.split()
            except Exception as e:
                bottle.abort(401, 'Auth Exception: %s' %(str(e)))
            enc_user_passwd = auth_hdr_val.split()[1]
            user_passwd = base64.b64decode(enc_user_passwd)
            user, passwd = user_passwd.split(':')
            if (not self._conf_info.get('admin_user') == user or
                not self._conf_info.get('admin_password') == passwd):
                bottle.abort(401, 'Authentication check failed')

            # Add admin role to the request
            bottle.request.environ['HTTP_X_ROLE'] = 'admin'
    # end __init__

    def start_http_server(self):
        self._http_app.run(
            host=self._http_host, port=self._http_port,
            server=get_bottle_server(self._conf_info.get('max_requests')))
    # end start_http_server
# end class LocalAuth

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
        if (env.get('PATH_INFO') and
            env['PATH_INFO'].startswith('/documentation')):
            app = bottle.app()
        else:
            app = self.app if self.mt else bottle.app()

        return app(env, start_response)

# Post-auth filter. Normalize user/role supplied by quantum plugin for
# consumption by Perms


class AuthPostKeystone(object):

    def __init__(self, app, conf):
        self.app = app
        self.conf = conf

    def __call__(self, env, start_response):

        # if rbac is set, skip old admin based MT
        if self.conf['auth_svc']._mt_rbac:
            return self.app(env, start_response)

        # only allow admin access when MT is on
        roles = []
        if 'HTTP_X_ROLE' in env:
            roles = env['HTTP_X_ROLE'].split(',')
        if not 'admin' in [x.lower() for x in roles]:
            start_response('403 Permission Denied',
                [('Content-type', 'text/plain')])
            return ['403 Permission Denied']

        return self.app(env, start_response)


class AuthServiceKeystone(object):

    def __init__(self, server_mgr, args):
        _kscertbundle=''
        if args.certfile and args.keyfile and args.cafile \
           and args.auth_protocol == 'https':
               certs=[args.certfile, args.keyfile, args.cafile]
               _kscertbundle=cfgmutils.getCertKeyCaBundle(_DEFAULT_KS_CERT_BUNDLE,certs)
        self._conf_info = {
            'auth_host': args.auth_host,
            'auth_port': args.auth_port,
            'auth_protocol': args.auth_protocol,
            'admin_user': args.admin_user,
            'admin_password': args.admin_password,
            'admin_tenant_name': args.admin_tenant_name,
            'admin_port': args.admin_port,
            'max_requests': args.max_requests,
            'insecure':args.insecure,
        }
        try:
            if 'v3' in args.auth_url:
                self._conf_info['auth_version'] = 'v3.0'
        except AttributeError:
            pass
        if _kscertbundle:
           self._conf_info['cafile'] = _kscertbundle
        self._server_mgr = server_mgr
        self._auth_method = args.auth
        self._auth_token = None
        self._auth_middleware = None
        self._mt_rbac = args.multi_tenancy_with_rbac
        self._multi_tenancy = args.multi_tenancy or args.multi_tenancy_with_rbac
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

    def json_request(self, method, path, retry_after_authn=False):
        if self._auth_token is None or self._auth_middleware is None:
            return {}
        headers = {'X-Auth-Token': self._auth_token}
        response, data = self._auth_middleware._json_request(
            method, path, additional_headers=headers)
        try:
            status_code = response.status_code
        except AttributeError:
            status_code = response.status

        # avoid multiple reauth
        if ((status_code == 401) and (not retry_after_authn)):
            try:
                self._auth_token = self._auth_middleware.get_admin_token()
                return self.json_request(method, path, retry_after_authn=True)
            except Exception as e:
                self._server_mgr.config_log(
                    "Error in getting admin token from keystone: " + str(e),
                    level=SandeshLevel.SYS_WARN)
                return {}

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
                msg = "Error in getting admin token: " + str(e)
                time.sleep(2)

        self._server_mgr.config_log("Auth token fetched from keystone.",
            level=SandeshLevel.SYS_NOTICE)

        # open access for troubleshooting
        admin_port = self._conf_info['admin_port']
        self._local_auth_app = LocalAuth(bottle.app(), self._conf_info)
        gevent.spawn(self._local_auth_app.start_http_server)

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
            # Retry verify after fetching the certs.
            try:
                self._auth_middleware.fetch_signing_cert()
                self._auth_middleware.fetch_ca_cert()
                return self._auth_middleware.verify_signed_token(user_token)
            except:
                return None
    # end

    # gets called from keystone middleware after token check
    def token_valid(self, env, start_response):
        status = env.get('HTTP_X_IDENTITY_STATUS')
        return True if status != 'Invalid' else False

    def validate_user_token(self, request):
        # following config forces keystone middleware to always return the result
        # back in HTTP_X_IDENTITY_STATUS env variable
        conf_info = self._conf_info.copy()
        conf_info['delay_auth_decision'] = True

        auth_middleware = auth_token.AuthProtocol(self.token_valid, conf_info)
        return auth_middleware(request.headers.environ, None)

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
