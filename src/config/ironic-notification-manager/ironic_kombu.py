#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
from __future__ import print_function
import re
from distutils.util import strtobool
import kombu
import gevent
import gevent.monkey
import json
gevent.monkey.patch_all()
import time
import signal
from gevent.queue import Queue
try:
    from gevent.lock import Semaphore
except ImportError:
    # older versions of gevent
    from gevent.coros import Semaphore

'''
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common import vnc_greenlets
'''
import ssl


class IronicKombuClient(object):

    def __init__(self, rabbit_server, rabbit_port,
                 rabbit_user, rabbit_password,
                 notification_level, ironic_notif_mgr_obj, **kwargs):
        self._rabbit_port = rabbit_port
        self._rabbit_user = rabbit_user
        self._rabbit_password = rabbit_password
        self._rabbit_hosts = self._parse_rabbit_hosts(rabbit_server)
        self._rabbit_ip = self._rabbit_hosts[0]["host"]
        self._notification_level = notification_level
        self._ironic_notification_manager = ironic_notif_mgr_obj
        self._conn_lock = Semaphore()

        # Register a handler for SIGTERM so that we can release the lock
        # Without it, it can take several minutes before new master is elected
        # If any app using this wants to register their own sigterm handler,
        # then we will have to modify this function to perhaps take an argument
        # gevent.signal(signal.SIGTERM, self.sigterm_handler)

        self._url = "amqp://%s:%s@%s:%s/" % (self._rabbit_user,
                                             self._rabbit_password,
                                             self._rabbit_ip,
                                             self._rabbit_port)
        msg = "Initializing RabbitMQ connection, urls %s" % self._url
        # self._conn_state = ConnectionStatus.INIT
        self._conn = kombu.Connection(self._url)
        self._exchange = self._set_up_exchange()
        self._queues = []
        self._queues = self._set_up_queues(self._notification_level)
        if not self._queues:
            exit()

    def _parse_rabbit_hosts(self, rabbit_servers):

        default_dict = {'user': self._rabbit_user,
                        'password': self._rabbit_password,
                        'port': self._rabbit_port}
        ret = []
        rabbit_hosts = re.compile('[,\s]+').split(rabbit_servers)
        for s in rabbit_hosts:
            match = re.match("(?:(?P<user>.*?)"
                             "(?::(?P<password>.*?))"
                             "*@)*(?P<host>.*?)(?::(?P<port>\d+))*$", s)
            if match:
                mdict = match.groupdict().copy()
                for key in ['user', 'password', 'port']:
                    if not mdict[key]:
                        mdict[key] = default_dict[key]

                ret.append(mdict)

        return ret

    def _set_up_exchange(self, exchange_name=None):
        if exchange_name:
            exchange = kombu.Exchange(str(exchange_name),
                                      type="topic", durable=False)
        else:
            exchange = kombu.Exchange("ironic", type="topic", durable=False)

    def _set_up_queues(self, notification_level):
        if notification_level not in ['info', 'debug', 'warning', 'error']:
            msg = "Unrecongized notification level: " + \
                  str(notification_level) + \
                  "\nPlease enter a valid notification level from: " \
                  "'info', 'debug', 'warning', 'error'"
            return 0
        sub_queue_names = []
        sub_queues = []
        log_levels = []
        if notification_level == "debug":
            log_levels = ['debug', 'info', 'warning', 'error']
        elif notification_level == "info":
            log_levels = ['info', 'warning', 'error']
        elif notification_level == "warning":
            log_levels = ['warning', 'error']
        elif notification_level == "error":
            log_levels = ['error']

        for level in log_levels:
            sub_queue_names.append('ironic_versioned_notifications.' +
                                   str(level))

        for sub_queue_name in sub_queue_names:
            sub_queues.append(kombu.Queue(str(sub_queue_name),
                              durable=False, exchange=self._exchange,
                              routing_key=str(sub_queue_name)))

        return sub_queues

    def _reconnect(self, delete_old_q=False):
        if self._conn_lock.locked():
            # either connection-monitor or publisher should have taken
            # the lock. The one who acquired the lock would re-establish
            # the connection and releases the lock, so the other one can
            # just wait on the lock, till it gets released
            self._conn_lock.wait()
            # if self._conn_state == ConnectionStatus.UP:
            #    return

        with self._conn_lock:
            msg = "RabbitMQ connection down"
            # self._logger(msg, level=SandeshLevel.SYS_NOTICE)
            # self._update_sandesh_status(ConnectionStatus.DOWN)
            # self._conn_state = ConnectionStatus.DOWN

            self._conn.close()

            self._conn.ensure_connection()
            self._conn.connect()

            # self._update_sandesh_status(ConnectionStatus.UP)
            # self._conn_state = ConnectionStatus.UP
            msg = 'RabbitMQ connection ESTABLISHED %s' % repr(self._conn)
            # self._logger(msg, level=SandeshLevel.SYS_NOTICE)

            self._channel = self._conn.channel()
            self._consumer = kombu.Consumer(self._conn,
                                            queues=self._queues,
                                            callbacks=[self._subscriber],
                                            accept=["application/json"])
    # end _reconnect

    def _connection_watch(self, connected, timeout=10000):
        if not connected:
            self._reconnect()

        while True:
            try:
                self._consumer.consume()
                self._conn.drain_events()
            except self._conn.connection_errors + self._conn.channel_errors:
                self._reconnect()
    # end _connection_watch

    def _connection_watch_forever(self, timeout=10000):
        connected = True
        while True:
            try:
                self._connection_watch(connected, timeout)
            except Exception as e:
                msg = 'Error in rabbitmq drainer greenlet: %s' % (str(e))
                print(msg)
                # avoid 'reconnect()' here as that itself might cause exception
                connected = False
    # end _connection_watch_forever

    def _process_message_dict(self, message_dict):
        return message_dict["event_type"]

    def _subscribe_cb(self, body):
        # print("The body is {}".format(body))
        message_dict = json.loads(str(body["oslo.message"]))
        # print("Message: \n" + str(message_dict))
        message_dict_payload = message_dict.pop("payload")
        ironic_object_data = message_dict_payload["ironic_object.data"]
        for k in message_dict:
            ironic_object_data[k] = message_dict[k]
        ironic_node_list = []
        ironic_node_list.append(ironic_object_data)
        self._ironic_notification_manager.process_ironic_node_info(
            ironic_node_list)

    def _subscriber(self, body, message):
        try:
            self._subscribe_cb(body)
            message.ack()
        except Exception as e:
            print("The error is " + str(e))

    def _start(self):
        self._reconnect()
        self._connection_watch_forever()

    def shutdown(self):
        self._conn.close()

