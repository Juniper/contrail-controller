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

OP_DELETE = "DELETE"


class ResourceWatcher(object):
    def __init__(self):
        self.obj_fields = []
        self.queues = []

    def add(self, client_queue, resource_type):
        requested_fields = client_queue.query.get(
            resource_type, {"fields": ["all"]})["fields"]
        self.obj_fields += requested_fields
        self.queues.append(client_queue)

    def remove(self, client_queue, resource_type):
        requested_fields = client_queue.query.get(
            resource_type, {"fields": ["all"]})["fields"]
        try:
            self.queues.remove(client_queue)
            for field in requested_fields:
                self.obj_fields.remove(field)
        except ValueError:
            pass


class EventDispatcher(object):
    _notification_queue = Queue()
    _watchers = {}
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
    def _set_watchers(cls, client_queues):
        cls._watchers = client_queues
    # end _set_watchers

    @classmethod
    def _get_watchers(cls):
        return cls._watchers
    # end _get_watchers

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
        cls._watchers.update({
            resource_type: cls._watchers.get(resource_type, ResourceWatcher())})
        cls._watchers.get(resource_type).add(client_queue, resource_type)
    # end _subscribe_client_queue

    @classmethod
    def _unsubscribe_client_queue(cls, client_queue, resource_type):
        cls._watchers.get(resource_type).remove(client_queue, resource_type)
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

    def dbe_obj_read(self, resource_type, resource_id, obj_fields):
        resource_oper = None
        try:
            if "all" in obj_fields:
                ok, obj_dict = self._db_conn.dbe_read(
                    resource_type,
                    resource_id,
                )
            else:
                obj_fields = list(set(obj_fields))
                ok, obj_dict = self._db_conn.dbe_read(
                    resource_type,
                    resource_id,
                    obj_fields=obj_fields
                )

            if not ok:
                resource_oper = "STOP"
                obj_dict = {
                    "error": "dbe_read failure: %s" % obj_dict
                }

        except NoIdError:
            err_msg = "Resource with id %s already deleted at the \
                point of dbe_read" % resource_id
            err_msg += detailed_traceback()
            self.config_log(
                err_msg,
                level=SandeshLevel.SYS_NOTICE
            )
            resource_oper = OP_DELETE
            obj_dict = {"uuid": resource_id}
        except Exception as e:
            err_msg = "Exception %s while performing dbe_read" % e
            err_msg += detailed_traceback()
            self.config_log(
                err_msg,
                level=SandeshLevel.SYS_NOTICE
            )
            resource_oper = "STOP"
            obj_dict = {
                "error": str(e)
            }
        return resource_oper, obj_dict
    # end dbe_obj_read

    def process_delete(self, resource_type, resource_id):
        object_type = resource_type.replace("_", "-")
        obj_dict = {"uuid": resource_id}
        for client_queue in self._watchers.get(resource_type).queues:
            event = self.pack(
                event=OP_DELETE,
                data={object_type: obj_dict}
            )
            client_queue.put_nowait(event)
    # process_delete

    def process_notification(self, notification, obj_fields, client_queues):
        if len(client_queues) == 0:
            return
        resource_type = notification.get("type")
        resource_oper = notification.get("oper")
        resource_id = notification.get("uuid")
        obj_dict = {}
        if resource_oper == OP_DELETE:
            return self.process_delete(resource_type, resource_id)

        resource_oper, obj_dict = self.dbe_obj_read(resource_type,
                                                    resource_id,
                                                    obj_fields)
        if resource_oper is None:
            #Set it to original value as no error occured
            resource_oper = notification.get("oper")
        object_type = resource_type.replace("_", "-")
        # [1]-New watchers might be have subscribed their queues
        # no need to send the event to these new watchers, as
        # they are recently subscribed after dbe_obj_read. Thus
        # initialize would have sent this event in init.
        #
        # [2]-Existing watchers might have unsubscribed their
        # queues, so send event only to the remaining queues
        # original list of queues.
        for client_queue in list(set(client_queues).intersection(set(
                self._watchers.get(resource_type).queues))):
            fields = client_queue.query.get(
                resource_type, {"fields": ["all"]})["fields"]
            if "all" in fields:
                event = self.pack(
                    event=resource_oper,
                    data={object_type: obj_dict}
                )
                client_queue.put_nowait(event)
                continue

            obj_with_fields = {
                'uuid': obj_dict.get("uuid"),
                'fq_name': obj_dict.get("fq_name"),
                'parent_type': obj_dict.get("parent_type"),
                'parent_uuid': obj_dict.get("parent_uuid")
            }
            for field in fields:
                obj_with_fields[field] = obj_dict.get(field)
            event = self.pack(
                event=resource_oper,
                data={object_type: obj_with_fields}
            )
            client_queue.put_nowait(event)
    # end process_notification

    def dispatch(self):
        while True:
            notification = self._notification_queue.get()
            resource_type = notification.get("type")
            if not self._watchers.get(resource_type, None):
                # No clients subscribed for this resource type
                continue
            if not self._watchers.get(resource_type).queues:
                # Last client in middle of unsubscribe
                continue
            obj_fields = self._watchers.get(resource_type).obj_fields
            if len(obj_fields) == 0:
                # All clients unsubscribed
                continue

            self.process_notification(
                notification, obj_fields,
                self._watchers.get(resource_type).queues[:])
    # end dispatch

    def initialize(self, resource_type, resource_query=None):
        object_type = resource_type.replace('_', '-')
        if not resource_query:
            resource_query = {}
        try:
            fields = resource_query.get("fields", None)
            if fields:
                ok, resources, marker = self._db_conn.dbe_list(
                    resource_type,
                    field_names=fields
                )
            else:
                ok, resources, marker = self._db_conn.dbe_list(
                    resource_type,
                    is_detail=True
                )

            if not ok:
                return ok, self.pack(
                    event="stop",
                    data={object_type + 's': resources}
                )

            for i, resource in enumerate(resources):
                resources[i] = {object_type: resource}

            return ok, self.pack(
                event="init",
                data={object_type + 's': resources}
            )
        except Exception as e:
            return False, self.pack(
                event="stop",
                data={
                    object_type + 's': [
                        {object_type: {"error": str(e)}}
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
