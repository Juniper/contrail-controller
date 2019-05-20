#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

def get_bottle_server(pool_size,
                      **kwargs):
    """
    Custom gevent pool server for bottle
    """
    from bottle import GeventServer
    from gevent.pool import Pool

    class GeventPoolServer(GeventServer):
        """
        Gevent server with limited pool size

        Arguments:
        tcp_keepalive_idle_time: The time (in seconds) the connection needs
        to remain idle before TCP starts sending keepalive probes

        tcp_keepalive_interval: The time (in seconds) between individual
        keepalive probes

        tcp_keepalive_probes: The maximum number of keepalive probes TCP
        should send before dropping the connection
        """
        def __init__(self, host='127.0.0.1', port=8080, **options):
            super(GeventPoolServer, self ).__init__(host, port,
                spawn=Pool(size=pool_size), **options)

            self.tcp_keepalive_idle_time = kwargs.pop('tcp_keepalive_idle_time',
                                                      7200)
            self.tcp_keepalive_interval = kwargs.pop('tcp_keepalive_interval',
                                                     75)
            self.tcp_keepalive_probes = kwargs.pop('tcp_keepalive_probes',
                                                   9)
        def run(self, handler):
            import threading, os, _socket
            from gevent import pywsgi, local
            if not isinstance(threading.local(), local.local):
                msg = "Bottle requires gevent.monkey.patch_all() (before import)"
                raise RuntimeError(msg)
            if self.options.pop('fast', None):
                depr('The "fast" option has been deprecated and removed by Gevent.')
            if self.quiet:
                self.options['log'] = None
            address = (self.host, self.port)
            server = pywsgi.WSGIServer(address, handler, **self.options)
            if 'BOTTLE_CHILD' in os.environ:
                import signal
                signal.signal(signal.SIGINT, lambda s, f: server.stop())
            server.init_socket()

            if hasattr(_socket,'SO_KEEPALIVE'):
                server.socket.setsockopt(_socket.SOL_SOCKET,
                                           _socket.SO_KEEPALIVE, 1)
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
            if hasattr(_socket, 'SO_REUSEADDR'):
                server.socket.setsockopt(_socket.SOL_SOCKET,
                                         _socket.SO_REUSEADDR, 1)
            server.serve_forever()

    return GeventPoolServer
