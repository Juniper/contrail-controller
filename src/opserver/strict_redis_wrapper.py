import socket
import redis
class StrictRedisWrapper(redis.StrictRedis):

    def __init__(self, *args, **kwargs):
        tcp_keepalive_opts = {socket.TCP_KEEPIDLE:3,socket.TCP_KEEPINTVL:1,\
                      socket.TCP_KEEPCNT:3}
        super(StrictRedisWrapper, self).__init__(*args, **kwargs,
                          socket_keepalive=True,\
                          socket_keepalive_options=tcp_keepalive_opts)
