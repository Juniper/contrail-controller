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
            {
                "event": resource_oper.lower(),
                "data": json.dumps({})
            },
            event
        )
    # end test_pack


class TestRegisterClient(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestRegisterClient, self).setUp()

    def tearDown(self):
        self._dispatcher._set_client_queues({})
        super(TestRegisterClient, self).tearDown()

    def test_subscribe_client(self):
        client_queue = Queue()
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        for resource_type in watcher_resource_types:
            self.assertEquals(
                [client_queue],
                self._dispatcher._get_client_queues()[resource_type]
            )

    def test_unsubscribe_client(self):
        client_queue = Queue()
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        self._dispatcher.unsubscribe_client(client_queue,
                                            watcher_resource_types)
        for resource_type in watcher_resource_types:
            self.assertEquals(
                [],
                self._dispatcher._get_client_queues().get(resource_type, [])
            )


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
            notification,
            self._dispatcher._get_notification_queue().get()
        )


class TestDispatch(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestDispatch, self).setUp()

    def tearDown(self):
        self._dispatcher._set_notification_queue(Queue())
        self._dispatcher._set_client_queues({})
        self._dispatcher._set_db_conn(None)
        super(TestDispatch, self).tearDown()

    def notify(self, notifications):
        for notification in notifications:
            self._dispatcher.notify_event_dispatcher(notification)

    def test_dispatch_no_dbe_read_failure_no_exception(self):
        mockVncDBClient = flexmock(
            dbe_read=lambda resource_type, resource_id: (True, {})
        )
        self._dispatcher._set_db_conn(mockVncDBClient)

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
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {
                "event": notification.get("oper").lower(),
                "data": "{}"
            }
            if notification.get("oper") == "DELETE":
                event.update(
                    {
                        "data": json.dumps(
                            {"virtual_network": {"uuid": "123"}}
                        )
                    }
                )
            self.assertEquals(
                event,
                client_queue.get()
            )

    def test_dispatch_with_dbe_read_failure(self):
        mockVncDBClient = flexmock(
            dbe_read=lambda resource_type, resource_id: (
                False, 'Mock read failure'))
        self._dispatcher._set_db_conn(mockVncDBClient)

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
            }
        ]

        client_queue = Queue()
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {"event": "stop", "data": json.dumps(
                {"virtual_network": {
                    "error": "dbe_read failure: Mock read failure"}})}
            self.assertEquals(
                event,
                client_queue.get()
            )

    def test_dispatch_with_exception(self):
        def raiseEx(ex):
            raise ex
        mockVncDBClient = flexmock(
            dbe_read=lambda resource_type, resource_id:
                raiseEx(Exception("unexpected exception"))
        )
        self._dispatcher._set_db_conn(mockVncDBClient)

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
            }
        ]

        client_queue = Queue()
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {
                "event": "stop",
                "data": json.dumps({
                    "virtual_network": {
                        "error": str(Exception("unexpected exception"))
                    }
                })
            }
            self.assertEquals(
                event,
                client_queue.get()
            )


class TestInitialize(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestInitialize, self).setUp()

    def tearDown(self):
        self._dispatcher._set_db_conn(None)
        super(TestInitialize, self).tearDown()

    def test_initialize_no_dbe_list_failure_no_exception(self):
        mockVncDBClient = flexmock(
            dbe_list=lambda resource_type, is_detail: (True, [{}], 0)
        )
        self._dispatcher._set_db_conn(mockVncDBClient)
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        for resource_type in watcher_resource_types:
            self.assertEqual(
                (
                    True,
                    {
                        "event": "init",
                        "data": json.dumps(
                            {
                                resource_type + 's': [{resource_type: {}}]
                            }
                        )
                    }
                ),
                self._dispatcher.initialize(resource_type)
            )

    def test_initialize_with_dbe_list_failure(self):
        mockVncDBClient = flexmock(
            dbe_list=lambda resource_type, is_detail: (
                False, "Mock list failure", 0))
        self._dispatcher._set_db_conn(mockVncDBClient)
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"
        ]
        for resource_type in watcher_resource_types:
            self.assertEqual(
                (
                    False,
                    {
                        "event": "stop",
                        "data": json.dumps(
                            {
                                resource_type + 's': "Mock list failure"
                            }
                        )
                    }
                ),
                self._dispatcher.initialize(resource_type)
            )

    def test_initialize_with_exception(self):
        def raiseEx(ex):
            raise ex
        mockVncDBClient = flexmock(
            dbe_list=lambda resource_type, is_detail:
                raiseEx(Exception("unexpected exception"))
        )
        self._dispatcher._set_db_conn(mockVncDBClient)
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"
        ]
        for resource_type in watcher_resource_types:
            self.assertEqual(
                (
                    False,
                    {
                        "event": "stop",
                        "data": json.dumps(
                            {
                                resource_type + 's': [
                                    {
                                        resource_type: {
                                            "error": "unexpected exception"
                                        }
                                    }
                                ]
                            }
                        )
                    }
                ),
                self._dispatcher.initialize(resource_type)
            )
