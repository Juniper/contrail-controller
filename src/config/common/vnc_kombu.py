#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import amqp.exceptions
import kombu
import gevent
import time

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class VncKombuClient(object):
    def connect(self, delete_old_q=False):
        msg = "Connecting to rabbitmq on %s:%s user %s" \
              % (self._rabbit_ip, self._rabbit_port, self._rabbit_user)
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.INIT)
        self._conn_state = ConnectionStatus.INIT
        while True:
            try:
                self._conn = kombu.Connection(hostname=self._rabbit_ip,
                                              port=self._rabbit_port,
                                              userid=self._rabbit_user,
                                              password=self._rabbit_password,
                                              virtual_host=self._rabbit_vhost)

                self._update_sandesh_status(ConnectionStatus.UP)
                self._conn_state = ConnectionStatus.UP
                msg = 'RabbitMQ connection ESTABLISHED'
                self._logger(msg, level=SandeshLevel.SYS_NOTICE)

                if delete_old_q:
                    bound_q = self._update_queue_obj(self._conn.channel())
                    try:
                        bound_q.delete()
                    except amqp.exceptions.ChannelError as e:
                        msg = "Unable to delete the old amqp Q: %s" % str(e)
                        self._logger(msg, level=SandeshLevel.SYS_ERR)

                self._obj_update_q = self._conn.SimpleQueue(self._update_queue_obj)

                old_subscribe_greenlet = self._subscribe_greenlet
                self._subscribe_greenlet = gevent.spawn(self._dbe_oper_subscribe)
                if old_subscribe_greenlet:
                    old_subscribe_greenlet.kill()

                break
            except Exception as e:
                if self._conn_state != ConnectionStatus.DOWN:
                    msg = "RabbitMQ connection down: %s" %(str(e))
                    self._logger(msg, level=SandeshLevel.SYS_ERR)

                self._update_sandesh_status(ConnectionStatus.DOWN)
                self._conn_state = ConnectionStatus.DOWN
                time.sleep(2)
    # end connect

    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, q_name, subscribe_cb, logger):
        self._rabbit_ip = rabbit_ip
        self._rabbit_port = rabbit_port
        self._rabbit_user = rabbit_user
        self._rabbit_password = rabbit_password
        self._rabbit_vhost = rabbit_vhost
        self._subscribe_cb = subscribe_cb
        self._logger = logger

        obj_upd_exchange = kombu.Exchange('vnc_config.object-update', 'fanout',
                                          durable=False)

        self._update_queue_obj = kombu.Queue(q_name, obj_upd_exchange)
        self._subscribe_greenlet = None
        self.connect(True)
    # end __init__

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnectionType.DATABASE,
            name='RabbitMQ', status=status, message=msg,
            server_addrs=["%s:%s" % (self._rabbit_ip, self._rabbit_port)])
    # end _update_sandesh_status

    def _dbe_oper_subscribe(self):
        msg = "Running greenlet _dbe_oper_subscribe"
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)

        with self._conn.SimpleQueue(self._update_queue_obj) as queue:
            while True:
                try:
                    message = queue.get()
                except Exception as e:
                    msg = "Disconnected from rabbitmq. Reinitializing connection: %s" % str(e)
                    self._logger(msg, level=SandeshLevel.SYS_WARN)
                    self.connect()
                    # never reached
                    continue

                trace = None
                try:
                    self._subscribe_cb(message.payload)
                except Exception as e:
                    msg = "Subscribe callback had error: %s" % str(e)
                    self._logger(msg, level=SandeshLevel.SYS_WARN)
                finally:
                    try:
                        message.ack()
                    except Exception as e:
                        msg = "Disconnected from rabbitmq. Reinitializing connection: %s" % str(e)
                        self._logger(msg, level=SandeshLevel.SYS_WARN)
                        self.connect()
                        # never reached
    #end _dbe_oper_subscribe

