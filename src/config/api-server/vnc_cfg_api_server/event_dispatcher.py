"""
This is the module that dispatches events to client queues registered by /watch api
The dispatcher receives events from rabbitmq event consumer and push events to specific client queues 
"""
from gevent.queue import Queue
import json

from .vnc_db import VncDbClient

from cfgm_common.vnc_greenlets import VncGreenlet

class EventDispatcher(object):
    _notify_queue = Queue()
    _client_queues = {}

    def __init__(self):
        VncGreentlet.spawn(self.dispatch)
    # end __init__

    def pack(self, oper, data):
        return {"event": oper, "data": json.dumps(data)}
    # end pack

    def dispatch(self):
        while True:
            notification = self._notify_queue.get()
            res_type = notification["type"]
            oper = notification["oper"]
            data = dbe_read(notification["type"], notification["uuid"])

            event = self.pack(oper, data)
            for client_queue in self._client_queues[res_type]:
                client_queue.put_nowait(event)
    # end dispatch

    def initialize(self, resource_type):
             return self.pack("init", self.dbe_list(resource_type))
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