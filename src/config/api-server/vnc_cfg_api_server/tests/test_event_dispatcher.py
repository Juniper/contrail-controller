from __future__ import absolute_import

import json
import gevent

from ..event_dispatcher import EventDispatcher

from testtools import TestCase
from flexmock import flexmock
from gevent.queue import Queue
gevent.monkey.patch_all()


class TestPack(TestCase):
    def setUp(self):
        super(TestPack, self).setUp()

    def tearDown(self):
        super(TestPack, self).tearDown()

    def test_pack(self):
        dispatcher = EventDispatcher()
        resource_oper = "CREATE"
        resource_data = {}
        event = dispatcher.pack(
            event=resource_oper,
            data=resource_data
        )
        self.assertEquals(
            event,
            {
                'event': resource_oper.lower(),
                'data': json.dumps({})
            }
        )
    # end test_pack


class TestRegisterClient(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestRegisterClient, self).setUp()

    def tearDown(self):
        self._dispatcher._set_client_queues({})
        super(TestRegisterClient, self).tearDown()

    def test_register_client(self):
        client_queue = Queue()
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        self._dispatcher.register_client(client_queue, watcher_resource_types)
        for resource_type in watcher_resource_types:
            self.assertEquals(self._dispatcher._get_client_queues()[
                              resource_type], [client_queue])


class TestNotifyEventDispatcher(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestNotifyEventDispatcher, self).setUp()

    def tearDown(self):
        self._dispatcher._set_notification_queue(Queue())
        super(TestNotifyEventDispatcher, self).tearDown()

    def test_notify_event_dispatcher(self):
        notification = {
            'oper': "CREATE",
            'type': "virtual_network",
            'uuid': "123"
        }
        self._dispatcher.notify_event_dispatcher(notification)
        self.assertEquals(
            self._dispatcher._get_notification_queue().get(),
            notification
        )


class TestDispatch(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        mockVncDBClient = flexmock(
            dbe_read=lambda resource_type, resource_id: {}
        )
        self._dispatcher._set_db_conn(mockVncDBClient)
        super(TestDispatch, self).setUp()

    def tearDown(self):
        self._dispatcher._set_notification_queue(Queue())
        self._dispatcher._set_client_queues({})
        self._dispatcher._set_db_conn(None)
        super(TestDispatch, self).tearDown()

    def test_dispatch(self):
        def notify(notifications):
            for notification in notifications:
                self._dispatcher.notify_event_dispatcher(notification)      
 
        notifications = [
                {
                    "oper": "CREATE",
                    "type": "virtual_network",
                    "uuid": "123"
                },
                {
                    "oper": "UPDATE",
                    "type": "virtual_network",
                    "uuid": "123"
                },
                {
                    "oper": "DELETE",
                    "type": "virtual_network",
                    "uuid": "123"
                },
            ]

        client_queue = Queue()
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.register_client(client_queue, watcher_resource_types)
        gevent.spawn(notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {
                    'event': notification.get("oper").lower(),
                    'data': "{}"
            }
            if notification.get("oper") == "DELETE":
                event.update(
                    {
                        'data': json.dumps(
                            {"virtual_network": {"uuid": "123"}}
                        )
                    }
                )
            self.assertEquals(
                client_queue.get(),
                event
            )


class TestInitialize(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        mockVncDBClient = flexmock(
            dbe_list=lambda resource_type: {}
        )
        self._dispatcher._set_db_conn(mockVncDBClient)
        super(TestInitialize, self).setUp()

    def tearDown(self):
        self._dispatcher._set_db_conn(None)
        super(TestInitialize, self).tearDown()

    def test_initialize(self):
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        for resource_type in watcher_resource_types:
            self.assertEqual(
                self._dispatcher.initialize(resource_type),
                {
                    'event': "init",
                    'data': "{}"
                }
            )
