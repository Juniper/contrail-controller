"""
This is the module that dispatches events to client queues registered by
/watch api

The dispatcher receives events from rabbitmq event consumer and push events
to specific client queues
"""
from __future__ import absolute_import

from gevent.queue import Queue
import json
from cfgm_common import vnc_greenlets


class EventDispatcher(object):
    _notification_queue = Queue()
    _client_queues = {}
    _db_conn = None

    @classmethod
    def _set_notification_queue(cls, notify_queue):
        cls._notification_queue = notify_queue
    # end _set_notification_queue

    @classmethod
    def _get_notification_queue(cls):
        return cls._notification_queue
    # end _get_notification_queue

    @classmethod
    def _set_client_queues(cls, client_queues):
        cls._client_queues = client_queues
    # end _set_client_queues

    @classmethod
    def _get_client_queues(cls):
        return cls._client_queues
    # end _get_client_queues

    @classmethod
    def _set_db_conn(cls, db_conn):
        cls._db_conn = db_conn
    # end _set_db_conn

    @classmethod
    def _get_db_conn(cls):
        return cls._db_conn
    # end _get_db_conn

    @classmethod
    def _register_client_queue(cls, client_queue, resource_type):
        cls._client_queues.update(
            {
                resource_type:
                    cls._client_queues.get(resource_type, []) + [client_queue]
            }
        )

    def __init__(self, spawn_dispatch_greenlet=False):
        if spawn_dispatch_greenlet:
            self._dispatch_greenlet = vnc_greenlets.VncGreenlet(
                "Event Dispatcher", self.dispatch)
    # end __init__

    def pack(self, event="", data={}):
        return {
            'event': event.lower(),
            'data': json.dumps(data)
        }
    # end pack

    def dispatch(self):
        while True:
            notification = self._notification_queue.get()
            resource_type = notification["type"]
            resource_oper = notification["oper"]
            resource_id = notification["uuid"]
            resource_data = {}
            if resource_oper != "DELETE":
                try:
                    resource_data = self._db_conn.dbe_read(
                        resource_type,
                        resource_id
                    )
                except NoIdError:
                    resource_oper = "DELETE"
                    resource_data = {resource_type: {"uuid": resource_id}}
            else:
                resource_data = {resource_type: {"uuid": resource_id}}

            event = self.pack(
                event=resource_oper,
                data=resource_data
            )
            for client_queue in self._client_queues.get(resource_type, []):
                client_queue.put_nowait(event)
    # end dispatch

    def initialize(self, resource_type):
        return self.pack(
            event="init",
            data=self._db_conn.dbe_list(resource_type)
        )
    # end initialize

    def register_client(self, client_queue, watch_resource_types):
        for resource_type in watch_resource_types:
            self._register_client_queue(client_queue, resource_type)
    # end register_client

    def notify_event_dispatcher(self, notification):
        self._notification_queue.put_nowait(notification)
    # end notify_event_dispatcher

# end class EventDispatcher
