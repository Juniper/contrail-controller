#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

def get_bottle_server(pool_size):
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

    return GeventPoolServer
