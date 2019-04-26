#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

def get_bottle_server(pool_size,
                      tcp_keepalive_enable=True,
                      tcp_keepalive_idle_time=7200,
                      tcp_keepalive_interval=75,
                      tcp_keepalive_probes=9):
    """
    Custom gevent pool server for bottle
    """
    from bottle import GeventServer
    from gevent.pool import Pool

    class GeventPoolServer(GeventServer):
        """ Gevent server with limited pool size
        """
        def __init__(self, host='127.0.0.1', port=8080, **options):
            super(GeventPoolServer, self ).__init__(host, port,
                spawn=Pool(size=pool_size), **options)

            self.tcp_keepalive_enable = tcp_keepalive_enable
            self.tcp_keepalive_idle_time = tcp_keepalive_idle_time
            self.tcp_keepalive_interval = tcp_keepalive_interval
            self.tcp_keepalive_probes = tcp_keepalive_probes

        def run(self, handler):
            import threading, os, _socket
            import cfgm_common
            from cfgm_common import vnc_wsgi
            from gevent import wsgi, pywsgi, local
            if not isinstance(threading.local(), local.local):
                msg = "Bottle requires gevent.monkey.patch_all() (before import)"
                raise RuntimeError(msg)
            if not self.options.pop('fast', None): wsgi = pywsgi
            self.options['log'] = None if self.quiet else 'default'
            address = (self.host, self.port)
            server = wsgi.WSGIServer(address, handler, **self.options)
            server.init_socket()

            if hasattr(_socket,'SO_KEEPALIVE'):
                server.socket.setsockopt(_socket.SOL_SOCKET,
                                           _socket.SO_KEEPALIVE,
                                           self.tcp_keepalive_enable)
            if hasattr(_socket, 'TCP_KEEPIDLE'):
                server.socket.setsockopt(_socket.IPPROTO_TCP,
                                           _socket.TCP_KEEPIDLE,
                                           self.tcp_keepalive_idle_time)
            if hasattr(_socket, 'TCP_KEEPINTVL'):
                server.socket.setsockopt(_socket.IPPROTO_TCP,
                                           _socket.TCP_KEEPINTVL,
                                           self.tcp_keepalive_interval)
            if hasattr(_socket, 'TCP_KEEPCNT'):
                server.socket.setsockopt(_socket.IPPROTO_TCP,
                                           _socket.TCP_KEEPCNT,
                                           self.tcp_keepalive_probes)
            if 'BOTTLE_CHILD' in os.environ:
                import signal
                signal.signal(signal.SIGINT, lambda s, f: server.stop())
            server.serve_forever()

    return GeventPoolServer
