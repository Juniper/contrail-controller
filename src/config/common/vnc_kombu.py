#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
import re
import amqp.exceptions
import kombu
import gevent
import gevent.monkey
gevent.monkey.patch_all()
import time
from gevent.queue import Queue
try:
    from gevent.lock import Semaphore
except ImportError: 
    # older versions of gevent
    from gevent.coros import Semaphore

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

__all__ = "VncKombuClient"


class VncKombuClientBase(object):
    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnectionType.DATABASE,
            name='RabbitMQ', status=status, message=msg,
            server_addrs=["%s:%s" % (self._rabbit_ip, self._rabbit_port)])
    # end _update_sandesh_status

    def publish(self, message):
        self._publish_queue.put(message)
    # end publish

    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger):
        self._rabbit_ip = rabbit_ip
        self._rabbit_port = rabbit_port
        self._rabbit_user = rabbit_user
        self._rabbit_password = rabbit_password
        self._rabbit_vhost = rabbit_vhost
        self._subscribe_cb = subscribe_cb
        self._logger = logger
        self._publish_queue = Queue()
        self._conn_lock = Semaphore()

        self.obj_upd_exchange = kombu.Exchange('vnc_config.object-update', 'fanout',
                                               durable=False)

    def num_pending_messages(self):
        return self._publish_queue.qsize()
    # end num_pending_messages

    def prepare_to_consume(self):
        # override this method
        return

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
        msg = 'RabbitMQ connection ESTABLISHED %s' % repr(self._conn)
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

    def _subscribe(self, body, message):
        try:
            self._subscribe_cb(body)
        finally:
            message.ack()


    def _start(self):
        self._can_consume = False
        self._reconnect()

        self._publisher_greenlet = gevent.spawn(self._publisher)
        self._connection_monitor_greenlet = gevent.spawn(self._connection_watch)

    def shutdown(self):
        self._publisher_greenlet.kill()
        self._connection_monitor_greenlet.kill()
        self._producer.close()
        self._consumer.close()
        self._conn.close()


class VncKombuClientV1(VncKombuClientBase):
    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger):
        super(VncKombuClientV1, self).__init__(rabbit_ip, rabbit_port,
                                               rabbit_user, rabbit_password,
                                               rabbit_vhost, rabbit_ha_mode,
                                               q_name, subscribe_cb, logger)

        self._conn = kombu.Connection(hostname=self._rabbit_ip,
                                      port=self._rabbit_port,
                                      userid=self._rabbit_user,
                                      password=self._rabbit_password,
                                      virtual_host=self._rabbit_vhost)
        self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange)
        self._start()
    # end __init__


class VncKombuClientV2(VncKombuClientBase):
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
        super(VncKombuClientV2, self).__init__(rabbit_hosts, rabbit_port,
                                               rabbit_user, rabbit_password,
                                               rabbit_vhost, rabbit_ha_mode,
                                               q_name, subscribe_cb, logger)

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
        queue_args = {"x-ha-policy": "all"} if rabbit_ha_mode else None
        self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange, queue_arguments=queue_args)

        self._start()
    # end __init__


from distutils.version import LooseVersion
if LooseVersion(kombu.__version__) >= LooseVersion("2.5.0"):
    VncKombuClient = VncKombuClientV2
else:
    VncKombuClient = VncKombuClientV1
