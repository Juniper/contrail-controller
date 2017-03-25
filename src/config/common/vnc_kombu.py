#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
import re
import amqp.exceptions
from distutils.util import strtobool
import kombu
import gevent
import gevent.monkey
gevent.monkey.patch_all()
import time
import signal
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
import ssl

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

    def sigterm_handler(self):
        self.shutdown()
        exit()

    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger,
                 **kwargs):
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
        self._ssl_params = self._fetch_ssl_params(**kwargs)

        # Register a handler for SIGTERM so that we can release the lock
        # Without it, it can take several minutes before new master is elected
        # If any app using this wants to register their own sigterm handler,
        # then we will have to modify this function to perhaps take an argument
        gevent.signal(signal.SIGTERM, self.sigterm_handler)

    def num_pending_messages(self):
        return self._publish_queue.qsize()
    # end num_pending_messages

    def prepare_to_consume(self):
        # override this method
        return

    def _reconnect(self, delete_old_q=False):
        if self._conn_lock.locked():
            # either connection-monitor or publisher should have taken
            # the lock. The one who acquired the lock would re-establish
            # the connection and releases the lock, so the other one can 
            # just wait on the lock, till it gets released
            self._conn_lock.wait()
            if self._conn_state == ConnectionStatus.UP:
                return

        with self._conn_lock:
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
            if delete_old_q:
                # delete the old queue in first-connect context
                # as db-resync would have caught up with history.
                try:
                    bound_q = self._update_queue_obj(self._channel)
                    bound_q.delete()
                except Exception as e:
                    msg = 'Unable to delete the old ampq queue: %s' %(str(e))
                    self._logger(msg, level=SandeshLevel.SYS_ERR)

            self._consumer = kombu.Consumer(self._channel,
                                           queues=self._update_queue_obj,
                                           callbacks=[self._subscribe])
            self._producer = kombu.Producer(self._channel, exchange=self.obj_upd_exchange)
    # end _reconnect

    def _delete_queue(self):
        # delete the queue
        try:
            bound_q = self._update_queue_obj(self._channel)
            if bound_q:
                bound_q.delete()
        except Exception as e:
            msg = 'Unable to delete the old ampq queue: %s' %(str(e))
            self._logger(msg, level=SandeshLevel.SYS_ERR)
    #end _delete_queue

    def _connection_watch(self, connected):
        if not connected:
            self._reconnect()

        self.prepare_to_consume()
        while True:
            try:
                self._consumer.consume()
                self._conn.drain_events()
            except self._conn.connection_errors + self._conn.channel_errors as e:
                self._reconnect()
    # end _connection_watch

    def _connection_watch_forever(self):
        connected = True
        while True:
            try:
                self._connection_watch(connected)
            except Exception as e:
                msg = 'Error in rabbitmq drainer greenlet: %s' %(str(e))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
                # avoid 'reconnect()' here as that itself might cause exception
                connected = False
    # end _connection_watch_forever

    def _publisher(self):
        message = None
        connected = True
        while True:
            try:
                if not connected:
                    self._reconnect()
                    connected = True

                if not message:
                    # earlier was sent fine, dequeue one more
                    message = self._publish_queue.get()

                while True:
                    try:
                        self._producer.publish(message)
                        message = None
                        break
                    except self._conn.connection_errors + self._conn.channel_errors as e:
                        self._reconnect()
            except Exception as e:
                log_str = "Error in rabbitmq publisher greenlet: %s" %(str(e))
                self._logger(log_str, level=SandeshLevel.SYS_ERR)
                # avoid 'reconnect()' here as that itself might cause exception
                connected = False
    # end _publisher

    def _subscribe(self, body, message):
        try:
            self._subscribe_cb(body)
        finally:
            message.ack()


    def _start(self):
        self._reconnect(delete_old_q=True)

        self._publisher_greenlet = gevent.spawn(self._publisher)
        self._connection_monitor_greenlet = gevent.spawn(self._connection_watch_forever)

    def shutdown(self):
        self._publisher_greenlet.kill()
        self._connection_monitor_greenlet.kill()
        self._producer.close()
        self._consumer.close()
        self._delete_queue()
        self._conn.close()

    _SSL_PROTOCOLS = {
        "tlsv1": ssl.PROTOCOL_TLSv1,
        "sslv23": ssl.PROTOCOL_SSLv23
    }

    @classmethod
    def validate_ssl_version(cls, version):
        version = version.lower()
        try:
            return cls._SSL_PROTOCOLS[version]
        except KeyError:
            raise RuntimeError('Invalid SSL version: {}'.format(version))

    def _fetch_ssl_params(self, **kwargs):
        if strtobool(str(kwargs.get('rabbit_use_ssl', False))):
            ssl_params = dict()
            ssl_version = kwargs.get('kombu_ssl_version', '')
            keyfile = kwargs.get('kombu_ssl_keyfile', '')
            certfile = kwargs.get('kombu_ssl_certfile', '')
            ca_certs = kwargs.get('kombu_ssl_ca_certs', '')
            if ssl_version:
                ssl_params.update({'ssl_version':
                    self.validate_ssl_version(ssl_version)})
            if keyfile:
                ssl_params.update({'keyfile': keyfile})
            if certfile:
                ssl_params.update({'certfile': certfile})
            if ca_certs:
                ssl_params.update({'ca_certs': ca_certs})
                ssl_params.update({'cert_reqs': ssl.CERT_REQUIRED})
            return ssl_params or True
        return False

class VncKombuClientV1(VncKombuClientBase):
    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger,
                 **kwargs):
        super(VncKombuClientV1, self).__init__(rabbit_ip, rabbit_port,
                                               rabbit_user, rabbit_password,
                                               rabbit_vhost, rabbit_ha_mode,
                                               q_name, subscribe_cb, logger,
                                               **kwargs)

        self._conn = kombu.Connection(hostname=self._rabbit_ip,
                                      port=self._rabbit_port,
                                      userid=self._rabbit_user,
                                      password=self._rabbit_password,
                                      virtual_host=self._rabbit_vhost)
        self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange, durable=False)
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
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger,
                 **kwargs):
        super(VncKombuClientV2, self).__init__(rabbit_hosts, rabbit_port,
                                               rabbit_user, rabbit_password,
                                               rabbit_vhost, rabbit_ha_mode,
                                               q_name, subscribe_cb, logger,
                                               **kwargs)

        _hosts = self._parse_rabbit_hosts(rabbit_hosts)
        self._urls = []
        for h in _hosts:
            h['vhost'] = "" if not rabbit_vhost else rabbit_vhost
            _url = "pyamqp://%(user)s:%(password)s@%(host)s:%(port)s/%(vhost)s" % h
            self._urls.append(_url)

        msg = "Initializing RabbitMQ connection, urls %s" % self._urls
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.INIT)
        self._conn_state = ConnectionStatus.INIT
        self._conn = kombu.Connection(self._urls, ssl=self._ssl_params)
        queue_args = {"x-ha-policy": "all"} if rabbit_ha_mode else None
        self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange,
                                             durable=False,
                                             queue_arguments=queue_args)

        self._start()
    # end __init__


from distutils.version import LooseVersion
if LooseVersion(kombu.__version__) >= LooseVersion("2.5.0"):
    VncKombuClient = VncKombuClientV2
else:
    VncKombuClient = VncKombuClientV1
