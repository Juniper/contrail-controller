#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
import re
import amqp.exceptions
import kombu
import gevent
gevent.monkey.patch_all()
import time
from gevent.queue import Queue

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

__all__ = "VncKombuClient"

class VncKombuClientV1(object):
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
                self._subscribe_greenlet = gevent.spawn(self._subscriber)
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
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger):
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
        self._publish_queue = Queue()
        self._publisher_greenlet = gevent.spawn(self._publisher)
        self._subscribe_greenlet = None
        self.connect(True)
    # end __init__

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnectionType.DATABASE,
            name='RabbitMQ', status=status, message=msg,
            server_addrs=["%s:%s" % (self._rabbit_ip, self._rabbit_port)])
    # end _update_sandesh_status

    def _subscriber(self):
        msg = "Running greenlet _subscriber"
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
    #end _subscriber

    def num_pending_messages(self):
        return self._publish_queue.qsize()
    # end num_pending_messages

    def prepare_to_consume(self):
        # override this method
        return

    def publish(self, message):
        if self._rabbit_vhost == "__NONE__":
            return
        self._publish_queue.put(message)
    # end publish

    def _obj_update_q_put(self, oper_info):
        if self._rabbit_vhost == "__NONE__":
            return
        self._publish_queue.put(oper_info)
    # end _obj_update_q_put

    def _publisher(self):
        self.prepare_to_consume()
        while True:
            try:
                message = self._publish_queue.get()
                while True:
                    try:
                        self._obj_update_q.put(message, serializer='json')
                        break
                    except Exception as e:
                        log_str = "Disconnected from rabbitmq. " + \
                            "Reinitializing connection: %s" % str(e)
                        self.config_log(log_str, level=SandeshLevel.SYS_WARN)
                        time.sleep(1)
                        self.connect()
            except Exception as e:
                log_str = "Unknown exception in _publisher greenlet" + str(e)
                self.config_log(log_str, level=SandeshLevel.SYS_ERR)
                time.sleep(1)
    # end _publisher


class VncKombuClientV2(object):
    def _reconnect(self):
        if self._conn_lock.locked():
            # either connection-monitor or publisher should have taken
            # the lock. The one who acquired the lock would re-establish
            # the connection and releases the lock, so the other one can 
            # just wait on the lock, till it gets released
            self._conn_lock.wait()
            return

        self._conn_lock.acquire()

        msg = "RabbitMQ connection down"
        self._logger(msg, level=SandeshLevel.SYS_ERR)
        self._update_sandesh_status(ConnectionStatus.DOWN)
        self._conn_state = ConnectionStatus.DOWN

        self._conn.close()

        self._conn.ensure_connection()
        self._conn.connect()

        self._update_sandesh_status(ConnectionStatus.UP)
        self._conn_state = ConnectionStatus.UP
        msg = 'RabbitMQ connection ESTABLISHED %s ' % str(self._conn._info())
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)

        self._channel = self._conn.channel()
        self._consumer = kombu.Consumer(self._channel,
                                       queues=self._update_queue_obj,
                                       callbacks=[self._subscribe])
        if self._can_consume:
            self._consumer.consume()
        self._producer = kombu.Producer(self._channel, exchange=self.obj_upd_exchange)

        self._conn_lock.release()
    # end _reconnect

    def _parse_rabbit_hosts(self, rabbit_hosts):
        server_list = rabbit_hosts.split(",")

        default_dict = {'user': self._rabbit_user,
                        'password': self._rabbit_password,
                        'port': self._rabbit_port}
        ret = []
        for s in server_list:
            match = re.match("(?:(?P<user>.*?)(?::(?P<password>.*?))*@)*(?P<host>.*?)(?::(?P<port>\d+))*$", s)
            if match:
                mdict = match.groupdict().copy()
                for key in ['user', 'password', 'port']:
                    if not mdict[key]:
                        mdict[key] = default_dict[key]

                ret.append(mdict)

        return ret

    def __init__(self, rabbit_hosts, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger):
        self._rabbit_port = rabbit_port
        self._rabbit_user = rabbit_user
        self._rabbit_password = rabbit_password
        self._rabbit_vhost = rabbit_vhost
        self._subscribe_cb = subscribe_cb
        self._logger = logger

        _hosts = self._parse_rabbit_hosts(rabbit_hosts)
        self._urls = []
        for h in _hosts:
            h['vhost'] = "" if not rabbit_vhost else rabbit_vhost
            _url = "pyamqp://%(user)s:%(password)s@%(host)s:%(port)s/%(vhost)s/" % h
            self._urls.append(_url)

        msg = "Initializing RabbitMQ connection, urls %s" % self._urls
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.INIT)
        self._conn_state = ConnectionStatus.INIT
        self._conn = kombu.Connection(self._urls)
        self.obj_upd_exchange = kombu.Exchange('vnc_config.object-update', 'fanout',
                                               durable=False)

        listen_port = self._db_client_mgr.get_server_port()

        queue_args = {"x-ha-policy": "all"} if rabbit_ha_mode else None
        self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange, queue_arguments=queue_args)

        self._publish_queue = Queue()
        self._publisher_greenlet = gevent.spawn(self._publisher)
        self._connection_monitor_greenlet = gevent.spawn(self._connection_watch)
        self._conn_lock = gevent.lock.Semaphore()

        if self._rabbit_vhost == "__NONE__":
            return

        self._can_consume = False
        self._reconnect()
    # end __init__

    def _connection_watch(self):
        self.prepare_to_consume()
        self._can_consume = True
        self._consumer.consume()
        while True:
            try:
                self._conn.drain_events()
            except self._conn.connection_errors + self._conn.channel_errors as e:
                self._reconnect()
    # end _connection_watch

    def publish(self, message):
        if self._rabbit_vhost == "__NONE__":
            return
        self._publish_queue.put(message)
    # end publish

    def _publisher(self):
        while True:
            try:
                message = self._publish_queue.get()
                while True:
                    try:
                        self._producer.publish(message)
                        break
                    except self._conn.connection_errors + self._conn.channel_errors as e:
                        self._reconnect()
            except Exception as e:
                log_str = "Unknown exception in _publisher greenlet" + str(e)
                self._logger(log_str, level=SandeshLevel.SYS_ERR)
    # end _publisher

    def num_pending_messages(self):
        return self._publish_queue.qsize()
    # end num_pending_messages

    def prepare_to_consume(self):
        # override this method
        return

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnectionType.DATABASE,
                               name='RabbitMQ', status=status, message=msg,
                               server_addrs=["%s" % self._urls])
    # end _update_sandesh_status

    def _subscribe(self, body, message):
        try:
            self._subscribe_cb(body)
        finally:
            message.ack()


from distutils.version import LooseVersion
if LooseVersion(kombu.__version__) >= LooseVersion("2.5.0"):
    VncKombuClient = VncKombuClientV2
else:
    VncKombuClient = VncKombuClientV1
