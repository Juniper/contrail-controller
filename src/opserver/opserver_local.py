#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import sys
import gevent
from gevent import monkey
monkey.patch_all()
import bottle
import base64
from bottle import GeventServer
from gevent.pool import Pool

class OpserverLocalStdLog(object):
    def __init__(self, server_name, http_port):
        self._server_name = server_name
        self._port = http_port

    def write(self, text):
        sys.stderr.write('[' + self._server_name + ':' + str(self._port) + ']' + text)

class GeventPoolServer(GeventServer):    
    def run(self, handler):
        from gevent import wsgi as wsgi_fast, pywsgi, monkey, local
        if self.options.get('monkey', True):
            import threading
            if not threading.local is local.local: monkey.patch_all()
        wsgi = wsgi_fast if self.options.get('fast') else pywsgi
        self._std_log = OpserverLocalStdLog("LOCAL API", self.port)
        self.srv = wsgi.WSGIServer((self.host, self.port), handler,
                          spawn=Pool(size=self.pool_size), log = self._std_log)
        self.srv.serve_forever()

    def set_pool_size(self, pool_size):
        self.pool_size = pool_size

# Open port for access to API server for trouble shooting
class LocalApp(object):

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
        self._poolserver = GeventPoolServer(host = self._http_host,
                                            port = self._http_port)
        self._poolserver.set_pool_size(1024)
        bottle.run(app = self._http_app, server = self._poolserver)
    # end start_http_server
# end class LocalApp
