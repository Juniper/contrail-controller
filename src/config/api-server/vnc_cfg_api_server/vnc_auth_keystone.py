from __future__ import absolute_import
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
#
# This file contains authentication/authorization functionality for VNC-CFG
# subsystem. It also currently contains keystone adaptation which can in
# future by moved to vnc_auth_keystone.py
#

from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import object
import gevent
from gevent import monkey
monkey.patch_all()
import bottle
import time
import base64
import re
import uuid

from keystonemiddleware import auth_token
from keystoneauth1 import exceptions as k_exc

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from .vnc_bottle import get_bottle_server
from cfgm_common.exceptions import NoIdError
from cfgm_common import utils as cfgmutils
from cfgm_common import vnc_greenlets
from .context import get_request, get_context, set_context, use_context
from .context import ApiContext, ApiInternalRequest

from .auth_context import set_auth_context, use_auth_context

#keystone SSL cert bundle
_DEFAULT_KS_CERT_BUNDLE="/tmp/keystonecertbundle.pem"
_DEFAULT_KS_VERSION = "v2.0"

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
            admin_user = self._conf_info.get('admin_user',
                    self._conf_info.get('username'))
            admin_password = self._conf_info.get('admin_password',
                    self._conf_info.get('password'))
            if (not admin_user == user or not admin_password == passwd):
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

    def __init__(self, app, conf, server_mgr):
        self.app = app
        self.conf = conf
        self.server_mgr = server_mgr

    @use_context
    def __call__(self, env, start_response):
        get_context().set_proc_time('PRE_KEYSTONE_REQ')
        if self.server_mgr.path_in_white_list(env['PATH_INFO']):
            # permit access to white list without requiring a token
            env['HTTP_X_DOMAIN_ID'] =\
                self.server_mgr.default_domain['uuid'].replace('-', '')
            env['HTTP_X_DOMAIN_NAME'] =\
                self.server_mgr.default_domain['fq_name'][-1]
            env['HTTP_X_PROJECT_ID'] =\
                self.server_mgr.default_project['uuid'].replace('-', '')
            env['HTTP_X_PROJECT_NAME'] =\
                self.server_mgr.default_project['fq_name'][-1]
            env['HTTP_X_ROLE'] = ''
            return self.server_mgr.api_bottle(env, start_response)
        elif self.server_mgr.is_auth_needed():
            try:
                return self.app(env, start_response)
            except k_exc.EmptyCatalog as e:
                # https://contrail-jws.atlassian.net/browse/CEM-7641
                # If API server started after Keystone was started and before
                # Keystone Endpoints were provisioned, the auth_token
                # middleware needs to be reloaded.
                msg = "Keystone auth_token middleware failed: %s" % str(e)
                self.server_mgr.sigterm_handler(msg)
        else:
            return self.server_mgr.api_bottle(env, start_response)


# Post-auth filter. Normalize user/role supplied by quantum plugin for
# consumption by Perms
class AuthPostKeystone(object):

    def __init__(self, server_mgr, auth_svc):
        self.server_mgr = server_mgr
        self.app = server_mgr.api_bottle
        self.auth_svc = auth_svc

    @use_auth_context
    def __call__(self, env, start_response):
        domain_id = (env.get('HTTP_X_DOMAIN_ID') or
                     env.get('HTTP_X_PROJECT_DOMAIN_ID') or
                     env.get('HTTP_X_USER_DOMAIN_ID'))
        # if there is not domain ID (aka Keystone v2) or if it equals to
        # 'default', fallback to the Contrail default-domain ID and name
        if not domain_id or domain_id == self.auth_svc.default_domain_id:
            env['HTTP_X_DOMAIN_ID'] =\
                self.server_mgr.default_domain['uuid'].replace('-', '')
            env['HTTP_X_DOMAIN_NAME'] =\
                self.server_mgr.default_domain['fq_name'][-1]
        else:
            # ensure to set HTTP_X_DOMAIN_ID as it is the header used in the
            # Contrail RBAC code and in certain setup, Keystone auth middleware
            # just sets HTTP_X_PROJECT_DOMAIN_ID and/or HTTP_X_USER_DOMAIN_ID
            # headers
            env['HTTP_X_DOMAIN_ID'] = domain_id

        get_context().set_proc_time('POST_KEYSTONE_REQ')

        set_auth_context(env)
        # if rbac is set, skip old admin based MT
        if self.auth_svc.mt_rbac:
            return self.app(env, start_response)

        # only allow admin access when MT is on
        roles = []
        if 'HTTP_X_ROLE' in env:
            roles = env['HTTP_X_ROLE'].split(',')
        if not 'admin' in [x.lower() for x in roles]:
            start_response('403 Permission Denied',
                [('Content-type', 'text/plain')])
            return '403 Permission Denied'.encode("latin-1")

        return self.app(env, start_response)


class AuthServiceKeystone(object):

    def __init__(self, server_mgr, args):
        self.args = args
        _kscertbundle=''
        if args.auth_protocol == 'https' and args.cafile:
            certs=[args.cafile]
            if args.keyfile and args.certfile:
                certs=[args.certfile, args.keyfile, args.cafile]
            _kscertbundle=cfgmutils.getCertKeyCaBundle(_DEFAULT_KS_CERT_BUNDLE,certs)
        self._conf_info = {
            'admin_port': args.admin_port,
            'max_requests': args.max_requests,
            'region_name': args.region_name,
            'insecure': args.insecure,
            'signing_dir': args.signing_dir,
        }
        if args.auth_url:
            auth_url = args.auth_url
        else:
            auth_url = '%s://%s:%s/%s' % (
                    args.auth_protocol, args.auth_host, args.auth_port,
                    _DEFAULT_KS_VERSION)
        if 'v2.0' in auth_url.split('/'):
            identity_uri = '%s://%s:%s' % (
                    args.auth_protocol, args.auth_host, args.auth_port)
            self._conf_info.update({
                'auth_host': args.auth_host,
                'auth_port': args.auth_port,
                'auth_protocol': args.auth_protocol,
                'admin_user': args.admin_user,
                'admin_password': args.admin_password,
                'admin_tenant_name': args.admin_tenant_name,
                'identity_uri': identity_uri})
        else:
            self._conf_info.update({
                'auth_type': args.auth_type,
                'auth_url': auth_url,
                'username': args.admin_user,
                'password': args.admin_password,
            })
            # Add user domain info
            self._conf_info.update(**cfgmutils.get_user_domain_kwargs(args))
            # Get project scope auth params
            scope_kwargs = cfgmutils.get_project_scope_kwargs(args)
            if not scope_kwargs:
                # Default to domain scoped auth
                scope_kwargs = cfgmutils.get_domain_scope_kwargs(args)
            self._conf_info.update(**scope_kwargs)

        if _kscertbundle:
            self._conf_info['cafile'] = _kscertbundle
        self._server_mgr = server_mgr
        self._auth_method = args.auth
        self._auth_middleware = None
        self.mt_rbac = server_mgr.is_rbac_enabled()
        self._auth_needed = server_mgr.is_auth_needed()
        if not self._auth_method:
            return
        if self._auth_method != 'keystone':
            raise UnknownAuthMethod()

        # map keystone id to users. Needed for quantum plugin because contrail
        # plugin doesn't have access to user token and ends up sending admin
        # admin token along with user-id and role
        self._ks_users = {}

        # configure memcache if enabled
        if self._auth_needed and 'memcache_servers' in args:
            self._conf_info[
                'memcached_servers'] = args.memcache_servers.split(',')
            if 'token_cache_time' in args:
                self._conf_info['token_cache_time'] = args.token_cache_time
        self._user_auth_middleware = None
        self._hdr_from_token_auth_middleware = None
        self.default_domain_id = args.default_domain_id
    # end __init__

    def get_middleware_app(self):
        if not self._auth_method:
            return None

        if not self._auth_needed:
            return None

        # keystone middleware is needed for fetching objects

        app = AuthPostKeystone(self._server_mgr, self)

        auth_middleware = auth_token.AuthProtocol(app, self._conf_info)
        self._auth_middleware = auth_middleware

        # open access for troubleshooting
        self._local_auth_app = LocalAuth(self._server_mgr.api_bottle,
                                         self._conf_info)
        vnc_greenlets.VncGreenlet("VNC Auth Keystone",
                                  self._local_auth_app.start_http_server)

        app = AuthPreKeystone(auth_middleware, None, self._server_mgr)
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
        token_info = env.get('keystone.token_info')
        start_response('200 OK', [('Content-type', 'text/plain')])
        return token_info if status != 'Invalid' else ''

    def start_response(self, status, headers, exc_info=None):
        pass

    def validate_user_token(self):
        if not self._user_auth_middleware:
            # following config forces keystone middleware to always return the
            # result back in HTTP_X_IDENTITY_STATUS env variable
            conf_info = self._conf_info.copy()
            conf_info['delay_auth_decision'] = True
            self._user_auth_middleware = auth_token.AuthProtocol(
                self.token_valid, conf_info)

        if not self._user_auth_middleware:
            return False, (403, " Permission denied")

        request_attrs = {
            'REQUEST_METHOD': get_request().route.method,
            'bottle.app': get_request().environ['bottle.app'],
        }
        if 'HTTP_X_AUTH_TOKEN' in get_request().environ:
            request_attrs['HTTP_X_AUTH_TOKEN'] =\
                get_request().environ['HTTP_X_AUTH_TOKEN'].encode("ascii")
        elif 'HTTP_X_USER_TOKEN' in get_request().environ:
            request_attrs['HTTP_X_USER_TOKEN'] =\
                get_request().environ['HTTP_X_USER_TOKEN'].encode("ascii")
        else:
            return False, (400, "User token needed for validation")
        b_req = bottle.BaseRequest(request_attrs)
        # get permissions in internal context
        orig_context = get_context()
        i_req = ApiInternalRequest(b_req.url, b_req.urlparts, b_req.environ,
                                   b_req.headers, None, None)
        set_context(ApiContext(internal_req=i_req))
        try:
            token_info = self._user_auth_middleware(
                get_request().headers.environ, self.start_response)
        finally:
            set_context(orig_context)

        return True, token_info

    def get_auth_headers_from_token(self, request, token):
        if not self._hdr_from_token_auth_middleware:
            conf_info = self._conf_info.copy()
            conf_info['delay_auth_decision'] = True

            def token_to_headers(env, start_response):
                start_response('200 OK', [('Content-type', 'text/plain')])
                status = env.get('HTTP_X_IDENTITY_STATUS')
                if status and status.lower() == 'invalid':
                    return {}
                ret_headers_dict = {}
                for hdr_name in ['HTTP_X_DOMAIN_ID', 'HTTP_X_PROJECT_ID',
                    'HTTP_X_PROJECT_NAME', 'HTTP_X_USER', 'HTTP_X_ROLE',
                    'HTTP_X_API_ROLE']:
                    hdr_val = env.get(hdr_name)
                    if hdr_val:
                        ret_headers_dict[hdr_name] = hdr_val
                return ret_headers_dict

            self._hdr_from_token_auth_middleware = auth_token.AuthProtocol(
                    token_to_headers, conf_info)
        return self._hdr_from_token_auth_middleware(
                request.headers.environ, self.start_response)
    # end get_auth_headers_from_token
# end class AuthService
