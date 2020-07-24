"""
This is the module that dispatches events to client queues subscribed by
/watch api

The dispatcher receives events from rabbitmq event consumer and push events
to specific client queues
"""
from __future__ import absolute_import

import json

from cfgm_common import vnc_greenlets
from cfgm_common.utils import detailed_traceback
from vnc_api.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from gevent.queue import Queue
from gevent import monkey
monkey.patch_all()


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
    def _subscribe_client_queue(cls, client_queue, resource_type):
        request_fields = client_queue.query.get(resource_type, {"fields": ["all"]})["fields"]
        cls._client_queues.update(
            {
                resource_type: {
                    "fields": cls._client_queues.get(resource_type, {"fields": []})["fields"] + request_fields,
                    "watchers": cls._client_queues.get(resource_type, {"watchers": []})["watchers"] + [client_queue]}
            }
        )
    # end _subscribe_client_queue

    @classmethod
    def _unsubscribe_client_queue(cls, client_queue, resource_type):
        try:
            request_fields = client_queue.query.get(resource_type, {"fields": ["all"]})["fields"]
            cls._client_queues.get(resource_type, []).get("watchers", []).remove(client_queue)
            for field in request_fields:
                cls._client_queues.get(resource_type, []).get("fields", []).remove(field)
        except ValueError:
            pass
    # end _unsubscribe_client_queue

    def __init__(self, db_client_mgr=None, spawn_dispatch_greenlet=False):
        if db_client_mgr:
            self._set_db_conn(db_client_mgr)
        if spawn_dispatch_greenlet:
            self._dispatch_greenlet = vnc_greenlets.VncGreenlet(
                "Event Dispatcher", self.dispatch)
    # end __init__

    def config_log(self, msg, level):
        self._db_conn.config_log(msg, level)
    # end config_log

    def pack(self, event="", data={}):
        return {
            "event": event.lower(),
            "data": json.dumps(data)
        }
    # end pack

    def dispatch(self):
        while True:
            notification = self._notification_queue.get()
            resource_type = notification.get("type")
            resource_oper = notification.get("oper")
            resource_id = notification.get("uuid")
            resource_data = {}
            if not self._client_queues.get(resource_type, None):
                # No clients subscribed for this resource type
                continue
            if not self._client_queues.get(resource_type).get("watchers", None):
                continue
            resource_fields = self._client_queues.get(resource_type).get("fields")
            if resource_oper == "DELETE":
                resource_data = {"uuid": resource_id}
            else:
                try:
                    if "all" in resource_fields:
                        ok, resource_data = self._db_conn.dbe_read(
                            resource_type,
                            resource_id,
                        )
                    else:
                        resource_fields = list(set(resource_fields))
                        ok, resource_data = self._db_conn.dbe_read(
                            resource_type,
                            resource_id,
                            obj_fields=resource_fields
                        )

                    if not ok:
                        resource_oper = "STOP"
                        resource_data = {
                            "error": "dbe_read failure: %s" % resource_data
                        }

                except NoIdError:
                    err_msg = "Resource with id %s already deleted at the \
                        point of dbe_read" % resource_id
                    err_msg += detailed_traceback()
                    self.config_log(
                        err_msg,
                        level=SandeshLevel.SYS_NOTICE
                    )
                    resource_oper = "DELETE"
                    resource_data = {"uuid": resource_id}
                except Exception as e:
                    err_msg = "Exception %s while performing dbe_read" % e
                    err_msg += detailed_traceback()
                    self.config_log(
                        err_msg,
                        level=SandeshLevel.SYS_NOTICE
                    )
                    resource_oper = "STOP"
                    resource_data = {
                        "error": str(e)
                    }

            object_type = resource_type.replace("_", "-")
            watcher_queue = self._client_queues.get(resource_type).get("watchers")

            for client_queue in watcher_queue:
                fields = client_queue.query.get(resource_type, {"fields": ["all"]})["fields"]
                if "all" in fields or resource_oper == "DELETE":
                    event = self.pack(
                        event=resource_oper,
                        data={object_type: resource_data}
                    )
                    client_queue.put_nowait(event)
                    continue

                resource_data_with_fields = {
                    'uuid': resource_data.get("uuid"),
                    'fq_name': resource_data.get("fq_name"),
                    'parent_type': resource_data.get("parent_type"),
                    'parent_uuid': resource_data.get("parent_uuid")
                }
                for field in fields:
                    resource_data_with_fields[field] = resource_data.get(field, {})
                event = self.pack(
                    event=resource_oper,
                    data={object_type: resource_data_with_fields}
                )
                client_queue.put_nowait(event)
    # end dispatch

    def initialize(self, resource_type):
        try:
            ok, resources, marker = self._db_conn.dbe_list(
                resource_type,
                is_detail=True
            )

            if not ok:
                return ok, self.pack(
                    event="stop",
                    data={resource_type + 's': resources}
                )

            for i, resource in enumerate(resources):
                resources[i] = {resource_type: resource}

            return ok, self.pack(
                event="init",
                data={resource_type + 's': resources}
            )
        except Exception as e:
            return False, self.pack(
                event="stop",
                data={
                    resource_type + 's': [
                        {resource_type: {"error": str(e)}}
                    ]
                }
            )
    # end initialize

    def subscribe_client(self, client_queue, watch_resource_types):
        for resource_type in watch_resource_types:
            self._subscribe_client_queue(client_queue, resource_type)
    # end subscribe_client

    def unsubscribe_client(self, client_queue, watch_resource_types):
        for resource_type in watch_resource_types:
            self._unsubscribe_client_queue(client_queue, resource_type)
    # end unsubscribe_client

    def notify_event_dispatcher(self, notification):
        self._notification_queue.put_nowait(notification)
    # end notify_event_dispatcher

# end class EventDispatcher