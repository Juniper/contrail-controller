#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

#
# StrictRedisWrapper
#
# Wrapper for StrictRedis Connections
#

import socket
import redis

class StrictRedisWrapper(redis.StrictRedis):

    def __init__(self, *args, **kwargs):
        tcp_keepalive_opts = {socket.TCP_KEEPIDLE:3,socket.TCP_KEEPINTVL:1,
                      socket.TCP_KEEPCNT:3}
        super(StrictRedisWrapper, self).__init__(*args,
                          socket_keepalive=True,
                          socket_keepalive_options=tcp_keepalive_opts,
                          **kwargs)
