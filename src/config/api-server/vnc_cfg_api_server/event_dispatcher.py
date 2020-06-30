"""
This is the module that dispatches events to client queues registered by
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
from cfgm_common.uve.vnc_api.ttypes import VncApiDebug, VncApiInfo,\
    VncApiNotice, VncApiError

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

    def config_log(self, msg_str, level=SandeshLevel.SYS_INFO):
        errcls = {
            SandeshLevel.SYS_DEBUG: VncApiDebug,
            SandeshLevel.SYS_INFO: VncApiInfo,
            SandeshLevel.SYS_NOTICE: VncApiNotice,
            SandeshLevel.SYS_ERR: VncApiError,
        }
        errcls.get(level, VncApiError)(
            api_msg=msg_str, level=level, sandesh=self._sandesh).send(
                sandesh=self._sandesh)
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
            if resource_oper != "DELETE":
                try:
                    ok, resource_data = self._db_conn.dbe_read(
                        resource_type,
                        resource_id
                    )
                    if not ok:
                        resource_oper = "ERROR"
                        resource_data = {
                            resource_type: {"error": "dbe_read failure"}
                        }
                except NoIdError:
                    err_msg = "Resource with id %s already deleted at the \
                        point of dbe_read" % resource_id
                    err_msg += detailed_traceback()
                    self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)
                    resource_oper = "DELETE"
                    resource_data = {resource_type: {"uuid": resource_id}}
                except Exception as e:
                    err_msg = "Exception %s while performing dbe_read" % e
                    err_msg += detailed_traceback()
                    self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)
                    resource_oper = "ERROR"
                    resource_data = {
                        resource_type: {"error": str(e)}
                    }
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
        try:
            ok, resources, marker = self._db_conn.dbe_list(
                resource_type,
                is_detail=True
            )

            for i, resource in enumerate(resources):
                resources[i] = {resource_type: resource}

            if not ok:
                return ok, self.pack(
                    event="error",
                    data={resource_type + 's': resources}
                )

            return ok, self.pack(
                event="init",
                data={resource_type + 's': resources}
            )
        except Exception as e:
            return False, self.pack(
                event="error",
                data={resource_type + 's': [
                        {resource_type: {"error": str(e)}}
                    ]
                }
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
