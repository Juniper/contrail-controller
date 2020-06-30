"""
This is the module that dispatches events to client queues registered by /watch api
The dispatcher receives events from rabbitmq event consumer and push events to specific client queues 
"""
from gevent.queue import Queue
import json

from .vnc_db import VncDbClient
from config.common.cfgm_common import vnc_greenlets

class EventDispatcher(object):
    _notify_queue = Queue()
    _client_queues = {}
    _db_conn = None

    @classmethod
    def _set_db_conn(cls, db_conn):
        _db_conn = db_conn
    # end _set_db_conn

    def __init__(self, spawn_dispatch_greenlet=False):
        if spawn_dispatch_greenlet:
            self._dispatch_greenlet = vnc_greenlets.VncGreenlet.spawn(self.dispatch)
    # end __init__

    def pack(self, resource_id, resource_oper, resource_data):
        return {
            'id': resource_id,
            'event': resource_oper,
            'data': json.dumps(resource_data)
        }
    # end pack

    def dispatch(self):
        while True:
            notification = self._notify_queue.get()
            resource_type = notification["type"]
            resource_oper = notification["oper"]
            resource_id = notification["uuid"]
            resource_data = _db_conn.dbe_read(resource_type, resource_id)

            event = self.pack(resource_id, resource_oper, resource_data)
            for client_queue in self._client_queues[resource_type]:
                client_queue.put_nowait(event)
    # end dispatch

    def initialize(self, resource_type)
        # TODO(dji): what does dbe_list return
        return self.pack("init", _db_conn.dbe_list(resource_type))
    # end initialize

    def register_client(self, client_queue, watch_resource_types):
        for resource_type in watch_resource_types:
            resource_type_queues = self._client_queues.get(resource_type, [])
            resource_type_queues.append(client_queue)
    # end register_client

    def notify_event_dispatcher(self, notification):
        self._notify_queue.put_nowait(notification)
    # end notify_event_dispatcher

# end class EventDispatcher