#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

import gevent
import kombu
import re
import ssl
from attrdict import AttrDict
from distutils.util import strtobool
from kombu.mixins import ConsumerMixin


class KombuAmqpClient(object):
    _SSL_PROTOCOLS = {
        "tlsv1": ssl.PROTOCOL_TLSv1,
        "sslv23": ssl.PROTOCOL_SSLv23
    }

    def __init__(self, logger, config, heartbeat=0):
        self._logger = logger
        servers = re.compile('[,\s]+').split(config.servers)
        urls = self._parse_servers(servers, config)
        ssl_params = self._fetch_ssl_params(config)
        self._queue_args = {"x-ha-policy": "all"} if config.ha_mode else None
        self._connection = kombu.Connection(urls, ssl=ssl_params,
                                            heartbeat=heartbeat)
        self._exchanges = {}
        self._consumers = {}
        self._running = False
        self._worker = None
        self._worker_gl = None
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
        if self._running:
            gevent.spawn(self._restart_worker)
        return consumer
    # end add_consumer

    def remove_consumer(self, name):
        if name not in self._consumers:
            raise ValueError("Consumer with name '%s' does not exist" % name)
        consumer = self._consumers.pop(name)
        if self._running:
            gevent.spawn(self._restart_worker)
        bound_queue = consumer.queue(self._connection.channel())
        if bound_queue is not None:
            bound_queue.delete()
    # end remove_consumer

    def publish(self, message, exchange, routing_key=None, **kwargs):
        exchange_obj = self.get_exchange(exchange)
        kombu.Producer(self._connection).publish(message, exchange=exchange_obj,
                                                 routing_key=routing_key,
                                                 **kwargs)
    # end publish

    def run(self):
        self._running = True
        self._restart_worker()
    # end run

    def stop(self):
        self._stop_worker(kill=True)
        self._running = False
    # end stop

    def close(self):
        channel = self._connection.channel()
        for consumer in self._consumers.values():
            bound_queue = consumer.queue(channel)
            if bound_queue is not None:
                bound_queue.delete()
        self._connection.close()
    # end close

    def _stop_worker(self, kill=False):
        if self._worker is not None:
            self._worker.stop()
            self._worker = None
        if self._worker_gl is not None:
            if kill:
                self._worker_gl.kill()
            else:
                self._worker_gl.join()
            self._worker_gl = None
    # end _stop_worker

    def _restart_worker(self):
        self._stop_worker()
        self._worker = Worker(self._connection, self._consumers.values())
        self._worker_gl = gevent.spawn(self._worker.run)
    # end _restart_worker

    @staticmethod
    def _parse_servers(servers, config):
        required_keys = ['user', 'password', 'port', 'vhost']
        urls = []
        for server in servers:
            match = re.match(
                "(?:(?P<user>.*?)(?::(?P<password>.*?))*@)*(?P<host>.*?)(?::(?P<port>\d+))*$",
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
            ssl_params['keyfile'] = config.keyfile
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

class Worker(ConsumerMixin):
    def __init__(self, connection, consumers):
        self.connection = connection
        self._consumers = consumers
    # end __init__

    def get_consumers(self, Consumer, channel):
        return [Consumer(queues=[c.queue],
                         callbacks=[c.callback]) for c in self._consumers]
    # end get_consumers

    def stop(self):
        self.should_stop = True
    # end stop

# end Worker
