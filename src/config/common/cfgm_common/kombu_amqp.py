#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

import gevent
import kombu
import re
import socket
import ssl
from attrdict import AttrDict
from gevent.event import Event
from gevent.lock import Semaphore
from gevent.queue import Queue
from kombu.utils import nested
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


class KombuAmqpClient(object):
    _SSL_PROTOCOLS = {
        "tlsv1": ssl.PROTOCOL_TLSv1,
        "sslv23": ssl.PROTOCOL_SSLv23
    }

    def __init__(self, logger, config, heartbeat=0):
        self._logger = logger
        servers = re.compile(r'[,\s]+').split(config.servers)
        urls = self._parse_servers(servers, config)
        ssl_params = self._fetch_ssl_params(config)
        self._queue_args = {"x-ha-policy": "all"} if config.ha_mode else None
        self._heartbeat = float(heartbeat)
        self._connection_lock = Semaphore()
        self._consumer_event = Event()
        self._publisher_queue = Queue()
        self._connection = kombu.Connection(urls, ssl=ssl_params,
            heartbeat=heartbeat, transport_options={'confirm_publish': True})
        self._connected = False
        self._exchanges = {}
        self._consumers = {}
        self._removed_consumers = []
        self._running = False
        self._consumers_changed = True
        self._consumer_gl = None
        self._publisher_gl = None
        self._heartbeat_gl = None
    # end __init__

    def get_exchange(self, name):
        return self._exchanges.get(name)
    # end get_exchange

    def add_exchange(self, name, type='direct', durable=False, **kwargs):
        if name in self._exchanges:
            raise ValueError("Exchange with name '%s' already exists" % name)
        exchange = kombu.Exchange(name, type=type, durable=durable, **kwargs)
        self._exchanges[name] = exchange
        return exchange
    # end add_exchange

    def add_consumer(self, name, exchange, routing_key='', callback=None,
                     durable=False, **kwargs):
        if name in self._consumers:
            raise ValueError("Consumer with name '%s' already exists" % name)
        exchange_obj = self.get_exchange(exchange)
        queue = kombu.Queue(name, exchange_obj, routing_key=routing_key,
                            durable=durable, **kwargs)
        consumer = AttrDict(queue=queue, callback=callback)
        self._consumers[name] = consumer
        self._consumer_event.set()
        self._consumers_changed = True
        msg = 'KombuAmqpClient: Added consumer: %s' % name
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
        return consumer
    # end add_consumer

    def remove_consumer(self, name):
        if name not in self._consumers:
            raise ValueError("Consumer with name '%s' does not exist" % name)
        consumer = self._consumers.pop(name)
        self._removed_consumers.append(consumer)
        self._consumer_event.set()
        self._consumers_changed = True
        msg = 'KombuAmqpClient: Removed consumer: %s' % name
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
    # end remove_consumer

    def publish(self, message, exchange, routing_key=None, **kwargs):
        if message is not None and isinstance(message, basestring) and \
                len(message) == 0:
            message = None
        msg = 'KombuAmqpClient: Publishing message to exchange %s, routing_key %s' % (exchange, routing_key)
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
        self._publisher_queue.put(AttrDict(message=message, exchange=exchange,
            routing_key=routing_key, kwargs=kwargs))
    # end publish

    def run(self):
        self._running = True
        self._consumer_gl = gevent.spawn(self._start_consuming)
        self._publisher_gl = gevent.spawn(self._start_publishing)
        if self._heartbeat:
            self._heartbeat_gl = gevent.spawn(self._heartbeat_check)
    # end run

    def stop(self):
        self._running = False
        if self._heartbeat_gl is not None:
            self._heartbeat_gl.kill()
        if self._publisher_gl is not None:
            self._publisher_gl.kill()
        if self._consumer_gl is not None:
            self._consumer_gl.kill()
        for consumer in (self._removed_consumers + self._consumers.values()):
            if consumer.queue.is_bound:
                consumer.queue.delete()
        self._connection.close()
    # end stop

    def _start_consuming(self):
        errors = (self._connection.connection_errors +
                  self._connection.channel_errors)
        removed_consumer = None
        msg = 'KombuAmqpClient: Starting consumer greenlet'
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
        while self._running:
            try:
                self._ensure_connection(self._connection, "Consumer")
                self._connected = True

                while len(self._removed_consumers) > 0 or removed_consumer:
                    if removed_consumer is None:
                        removed_consumer = self._removed_consumers.pop(0)
                    if removed_consumer.queue.is_bound:
                        removed_consumer.queue.delete()
                    removed_consumer = None

                if len(self._consumers.values()) == 0:
                    msg = 'KombuAmqpClient: Waiting for consumer'
                    self._logger(msg, level=SandeshLevel.SYS_DEBUG)
                    self._consumer_event.wait()
                    self._consumer_event.clear()

                if not self._running or len(self._consumers.values()) == 0:
                    msg = 'KombuAmqpClient: No consumers found'
                    self._logger(msg, level=SandeshLevel.SYS_DEBUG)
                    continue

                consumers = [kombu.Consumer(self._connection, queues=c.queue,
                             callbacks=[c.callback] if c.callback else None)
                             for c in self._consumers.values()]
                msg = 'KombuAmqpClient: Created consumers %s' % str(self._consumers.keys())
                self._logger(msg, level=SandeshLevel.SYS_DEBUG)
                self._consumers_changed = False
                with nested(*consumers):
                    while self._running and not self._consumers_changed:
                        try:
                            self._connection.drain_events(timeout=1)
                        except socket.timeout:
                            pass
            except errors as e:
                msg = 'KombuAmqpClient: Connection error in Kombu amqp consumer greenlet: %s' % str(e)
                self._logger(msg, level=SandeshLevel.SYS_WARN)
                self._connected = False
                gevent.sleep(0.1)
            except Exception as e:
                msg = 'KombuAmqpClient: Error in Kombu amqp consumer greenlet: %s' % str(e)
                self._logger(msg, level=SandeshLevel.SYS_ERR)
                self._connected = False
                gevent.sleep(0.1)
        msg = 'KombuAmqpClient: Exited consumer greenlet'
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
    # end _start_consuming

    def _start_publishing(self):
        errors = (self._connection.connection_errors +
                  self._connection.channel_errors)
        payload = None
        connection = self._connection.clone()
        msg = 'KombuAmqpClient: Starting publisher greenlet'
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
        while self._running:
            try:
                self._ensure_connection(connection, "Publisher")
                producer = kombu.Producer(connection)
                while self._running:
                    if payload is None:
                        payload = self._publisher_queue.get()

                    exchange = self.get_exchange(payload.exchange)
                    producer.publish(payload.message, exchange=exchange,
                        routing_key=payload.routing_key, **payload.kwargs)
                    payload = None
            except errors as e:
                msg = 'KombuAmqpClient: Connection error in Kombu amqp publisher greenlet: %s' % str(e)
                self._logger(msg, level=SandeshLevel.SYS_WARN)
            except Exception as e:
                msg = 'KombuAmqpClient: Error in Kombu amqp publisher greenlet: %s' % str(e)
                self._logger(msg, level=SandeshLevel.SYS_ERR)
        msg = 'KombuAmqpClient: Exiting publisher greenlet'
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
    # end _start_publishing

    def _heartbeat_check(self):
        while self._running:
            try:
                if self._connected and len(self._consumers.values()) > 0:
                    self._connection.heartbeat_check()
            except Exception as e:
                msg = 'KombuAmqpClient: Error in Kombu amqp heartbeat greenlet: %s' % str(e)
                self._logger(msg, level=SandeshLevel.SYS_DEBUG)
            finally:
                gevent.sleep(float(self._heartbeat)/2)
    # end _heartbeat_check

    def _ensure_connection(self, connection, name):
        msg = 'KombuAmqpClient: Ensuring %s connection' % name
        self._logger(msg, level=SandeshLevel.SYS_DEBUG)
        connection.close()
        connection.ensure_connection()
        connection.connect()
        msg = 'KombuAmqpClient: %s connection established %s' %\
            (name, str(self._connection))
        self._logger(msg, level=SandeshLevel.SYS_INFO)
    # end _ensure_connection

    @staticmethod
    def _parse_servers(servers, config):
        required_keys = ['user', 'password', 'port', 'vhost']
        urls = []
        for server in servers:
            match = re.match(
                r"(?:(?P<user>.*?)(?::(?P<password>.*?))*@)*(?P<host>.*?)(?::(?P<port>\d+))*$",
                server)
            if match:
                host = match.groupdict().copy()
                for key in required_keys:
                    if key not in host or host[key] is None:
                        host[key] = config[key]
                url = "pyamqp://%(user)s:%(password)s@%(host)s:%(port)s/%(vhost)s" % host
                urls.append(url)
        return urls
    # end _parse_servers

    @classmethod
    def _fetch_ssl_params(cls, config):
        if not config.use_ssl:
            return False
        ssl_params = dict()
        if config.ssl_version:
            ssl_params['ssl_version'] = cls._validate_ssl_version(
                config.ssl_version)
        if config.ssl_keyfile:
            ssl_params['keyfile'] = config.ssl_keyfile
        if config.ssl_certfile:
            ssl_params['certfile'] = config.ssl_certfile
        if config.ssl_ca_certs:
            ssl_params['ca_certs'] = config.ssl_ca_certs
            ssl_params['cert_reqs'] = ssl.CERT_REQUIRED
        return ssl_params or True
    # end _fetch_ssl_params

    @classmethod
    def _validate_ssl_version(cls, version):
        version = version.lower()
        try:
            return cls._SSL_PROTOCOLS[version]
        except KeyError:
            raise RuntimeError('Invalid SSL version: {}'.format(version))
    # end _validate_ssl_version

# end KombuAmqpClient
