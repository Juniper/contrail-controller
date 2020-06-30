from __future__ import absolute_import
from ..event_dispatcher import EventDispatcher
import gevent
from testtools import TestCase
from gevent.queue import Queue
from flexmock import flexmock
import json


class TestPack(TestCase):
    def setUp(self):
        super(TestInitialize, self).setUp()

    def tearDown(self):
        super(TestInitialize, self).tearDown()

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
        super(TestInitialize, self).setUp()

    def tearDown(self):
        self._dispatcher._set_client_queues({})
        super(TestInitialize, self).tearDown()

    def test_register_client(self):
        client_queue = Queue()
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        self._dispatcher.register_client(client_queue, watcher_resource_types)
        for resource_type in watcher_resource_types:
            self.assertEquals(self._dispatcher._get_client_queues()[
                              resource_type], client_queue)


class TestNotifyEventDispatcher(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestInitialize, self).setUp()

    def tearDown(self):
        self._dispatcher._set_notify_queue(Queue())
        super(TestInitialize, self).tearDown()

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
        super(TestInitialize, self).setUp()

    def tearDown(self):
        self._dispatcher._set_notification_queue(Queue())
        self._dispatcher._set_client_queues({})
        self._set_db_conn(None)
        super(TestInitialize, self).tearDown()

    def test_dispatch(self):
        client_queue = Queue()
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.register_client(client_queue, watcher_resource_types)
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
        greenlet = gevent.spawn(self._dispatcher.dispatch())
        for notification in notifications:
            self._dispatcher.notify_event_dispatcher(notification)
        gevent.kill(greenlet)
        for notification in notifications:
            self.assertEquals(
                client_queue.get(),
                {
                    'event': notification["oper"].lower(),
                    'data': "{}"
                }
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
        self._set_db_conn(None)
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
