<<<<<<< HEAD   (315a0a Provide user the option to disable router-id in flow hash ca)
=======
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
import re
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
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
try:
    from gevent.hub import ConcurrentObjectUseError
except:
    from gevent.exceptions import ConcurrentObjectUseError
from cfgm_common import vnc_greenlets
import ssl

__all__ = "VncKombuClient"


class VncKombuClientBase(object):
    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnType.DATABASE,
            name='RabbitMQ', status=status, message=msg,
            server_addrs=self._server_addrs)
    # end _update_sandesh_status

    def publish(self, message):
        self._publish_queue.put(message)
    # end publish

    def sigterm_handler(self):
        self.shutdown()
        exit()

    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger,
                 heartbeat_seconds=0, register_handler=True, **kwargs):
        self._rabbit_ip = rabbit_ip
        self._rabbit_port = rabbit_port
        self._rabbit_user = rabbit_user
        self._rabbit_password = rabbit_password
        self._rabbit_vhost = rabbit_vhost
        self._subscribe_cb = subscribe_cb
        self._logger = logger
        self._publish_queue = Queue()
        self._running = False
        self._heartbeat_seconds = heartbeat_seconds

        self.obj_upd_exchange = kombu.Exchange('vnc_config.object-update', 'fanout',
                                               durable=False)
        self._ssl_params = self._fetch_ssl_params(**kwargs)

        # Register a handler for SIGTERM so that we can release the lock
        # Without it, it can take several minutes before new master is elected
        # If any app using this wants to register their own sigterm handler,
        # then we will have to modify this function to perhaps take an argument
        if register_handler:
            gevent.signal(signal.SIGTERM, self.sigterm_handler)

    def num_pending_messages(self):
        return self._publish_queue.qsize()
    # end num_pending_messages

    def prepare_to_consume(self):
        # override this method
        return

    def _reconnect_drain(self, delete_old_q=False):
        msg = "RabbitMQ drainer connection down"
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.DOWN)
        self._conn_state = ConnectionStatus.DOWN

        self._conn_drain.close()

        self._conn_drain.ensure_connection()
        self._conn_drain.connect()

        self._update_sandesh_status(ConnectionStatus.UP)
        self._conn_state = ConnectionStatus.UP
        msg = 'RabbitMQ drainer connection ESTABLISHED %s' % repr(self._conn_drain)
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)

        self._channel_drain = self._conn_drain.channel()
        if self._subscribe_cb is not None:
            if delete_old_q:
                # delete the old queue in first-connect context
                # as db-resync would have caught up with history.
                try:
                    bound_q = self._update_queue_obj(self._channel_drain)
                    bound_q.delete()
                except Exception as e:
                    msg = 'Unable to delete the old ampq queue: %s' %(str(e))
                    self._logger(msg, level=SandeshLevel.SYS_ERR)

            self._consumer = kombu.Consumer(self._channel_drain,
                                           queues=self._update_queue_obj,
                                           callbacks=[self._subscribe])
    # end _reconnect_drain

    def _reconnect_publish(self):
        msg = "RabbitMQ publish connection down"
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.DOWN)
        self._conn_state = ConnectionStatus.DOWN

        self._conn_publish.close()

        self._conn_publish.ensure_connection()
        self._conn_publish.connect()

        self._update_sandesh_status(ConnectionStatus.UP)
        self._conn_state = ConnectionStatus.UP
        msg = 'RabbitMQ publish connection ESTABLISHED %s' % repr(self._conn_publish)
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)

        self._channel_publish = self._conn_publish.channel()
        self._producer = kombu.Producer(self._channel_publish, exchange=self.obj_upd_exchange)
    # end _reconnect_publish


    def _delete_queue(self):
        # delete the queue
        try:
            bound_q = self._update_queue_obj(self._channel_drain)
            if bound_q:
                bound_q.delete()
        except Exception as e:
            msg = 'Unable to delete the old ampq queue: %s' %(str(e))
            self._logger(msg, level=SandeshLevel.SYS_ERR)
    #end _delete_queue

    def _connection_watch(self, connected):
        if not connected:
            self._reconnect_drain()

        self.prepare_to_consume()
        while self._running:
            try:
                self._consumer.consume()
                self._conn_drain.drain_events()
            except self._conn_drain.connection_errors + self._conn_drain.channel_errors as e:
                self._reconnect_drain()
    # end _connection_watch

    def _connection_watch_forever(self):
        connected = True
        while self._running:
            try:
                self._connection_watch(connected)
            except Exception as e:
                msg = 'Error in rabbitmq drainer greenlet: %s' %(str(e))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
                # avoid 'reconnect()' here as that itself might cause exception
                connected = False
    # end _connection_watch_forever

    def _connection_heartbeat(self):
        while self._running:
            try:
                if self._conn_drain.connected:
                    self._conn_drain.heartbeat_check()
            except Exception as e:
                msg = 'Error in rabbitmq heartbeat greenlet for drain: %s' %(str(e))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
            finally:
                gevent.sleep(float(self._heartbeat_seconds/2))
    # end _connection_heartbeat

    def _publisher(self):
        message = None
        while self._running:
            try:
                self._reconnect_publish()

                while self._running:
                    if not message:
                        # earlier was sent fine, dequeue one more
                        message = self._publish_queue.get()
                    self._producer.publish(message)
                    message = None
            except self._conn_publish.connection_errors + self._conn_publish.channel_errors as e:
                # No need to print these errors in log
                # Don't reconnect, it will be done as part of the outer for loop above
                pass
            except Exception as e:
                log_str = "Error in rabbitmq publisher greenlet: %s" %(str(e))
                self._logger(log_str, level=SandeshLevel.SYS_ERR)
    # end _publisher

    def _subscribe(self, body, message):
        try:
            self._subscribe_cb(body)
        finally:
            message.ack()


    def _start(self, client_name):
        self._running = True
        self._reconnect_drain(delete_old_q=True)
        self._reconnect_publish()

        self._publisher_greenlet = vnc_greenlets.VncGreenlet(
                                               'Kombu ' + client_name,
                                               self._publisher)
        self._connection_monitor_greenlet = vnc_greenlets.VncGreenlet(
                                               'Kombu ' + client_name + '_ConnMon',
                                               self._connection_watch_forever)
        if self._heartbeat_seconds:
            self._connection_heartbeat_greenlet = vnc_greenlets.VncGreenlet(
                'Kombu ' + client_name + '_ConnHeartBeat',
                self._connection_heartbeat)
        else:
            self._connection_heartbeat_greenlet = None

    def greenlets(self):
        ret = [self._publisher_greenlet, self._connection_monitor_greenlet]
        if self._connection_heartbeat_greenlet:
            ret.append(self._connection_heartbeat_greenlet)
        return ret

    def shutdown(self):
        self._running = False
        self._publisher_greenlet.kill()
        self._connection_monitor_greenlet.kill()
        if self._connection_heartbeat_greenlet:
            self._connection_heartbeat_greenlet.kill()
        if self._consumer:
            self._delete_queue()
        self._conn_drain.close()
        self._conn_publish.close()

    def reset(self):
        self._publish_queue = Queue()

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
        self._server_addrs = ["%s:%s" % (self._rabbit_ip, self._rabbit_port)]

        self._conn = kombu.Connection(hostname=self._rabbit_ip,
                                      port=self._rabbit_port,
                                      userid=self._rabbit_user,
                                      password=self._rabbit_password,
                                      virtual_host=self._rabbit_vhost)
        if q_name:
            self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange, durable=False)
        self._start(q_name)
    # end __init__


class VncKombuClientV2(VncKombuClientBase):
    def _parse_rabbit_hosts(self, rabbit_hosts):

        default_dict = {'user': self._rabbit_user,
                        'password': self._rabbit_password,
                        'port': self._rabbit_port}
        ret = []
        for s in rabbit_hosts:
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
                 heartbeat_seconds=0, **kwargs):
        super(VncKombuClientV2, self).__init__(rabbit_hosts, rabbit_port,
                                               rabbit_user, rabbit_password,
                                               rabbit_vhost, rabbit_ha_mode,
                                               q_name, subscribe_cb, logger,
                                               heartbeat_seconds, **kwargs)
        self._server_addrs = re.compile('[,\s]+').split(rabbit_hosts)

        _hosts = self._parse_rabbit_hosts(self._server_addrs)
        self._urls = []
        for h in _hosts:
            h['vhost'] = "" if not rabbit_vhost else rabbit_vhost
            _url = "pyamqp://%(user)s:%(password)s@%(host)s:%(port)s/%(vhost)s" % h
            self._urls.append(_url)

        msg = "Initializing RabbitMQ connection, urls %s" % self._urls
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.INIT)
        self._conn_state = ConnectionStatus.INIT
        self._conn_drain = kombu.Connection(self._urls, ssl=self._ssl_params,
                                      heartbeat=heartbeat_seconds,
                                      transport_options={'confirm_publish': True})
        self._conn_publish = kombu.Connection(self._urls, ssl=self._ssl_params,
                                      heartbeat=heartbeat_seconds,
                                      transport_options={'confirm_publish': True})
        queue_args = {"x-ha-policy": "all"} if rabbit_ha_mode else None
        if q_name:
            self._update_queue_obj = kombu.Queue(q_name, self.obj_upd_exchange,
                                                 durable=False,
                                                 queue_arguments=queue_args)
        self._start(q_name)
    # end __init__


from distutils.version import LooseVersion
if LooseVersion(kombu.__version__) >= LooseVersion("2.5.0"):
    VncKombuClient = VncKombuClientV2
else:
    VncKombuClient = VncKombuClientV1
>>>>>>> CHANGE (48531d [Config] Have different connections for publisher/drainer in)
