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


class MockSubscribeAndUnsubscribe():
    def __init__(self):
        self.HOLD_API = True
        self._dispatcher = EventDispatcher()
        self.org_subscribe_client_queue = self._dispatcher._subscribe_client_queue
        self.org_unsubscribe_client_queue = self._dispatcher._unsubscribe_client_queue

    def mock_subscribe_client_queue(self, client_queue, resource_type):
        while self.HOLD_API:
            gevent.sleep(0.5)
        return self.org_subscribe_client_queue(client_queue, resource_type)

    def mock_unsubscribe_client_queue(self, client_queue, resource_type):
        while self.HOLD_API:
            gevent.sleep(0.5)
        return self.org_unsubscribe_client_queue(client_queue, resource_type)


class WatcherQueue(gevent.queue.Queue):
    def __init__(self, query):
        self.query = query
        super(WatcherQueue, self).__init__()


class TestRegisterClient(TestCase):
    def setUp(self):
        self._dispatcher = EventDispatcher()
        super(TestRegisterClient, self).setUp()

    def tearDown(self):
        self._dispatcher._set_watchers({})
        super(TestRegisterClient, self).tearDown()

    def test_concurrent_requests_subscribe_client_with_fields_xy_xz(self):
        def _execute_subscribe_concurrently(
                client_queue_list, watcher_resource_types):

            mock_subscribe = MockSubscribeAndUnsubscribe()
            self._dispatcher._subscribe_client_queue = mock_subscribe.mock_subscribe_client_queue
            for client_queue in client_queue_list:
                gevent.spawn(
                    self._dispatcher.subscribe_client,
                    client_queue,
                    watcher_resource_types)
            gevent.sleep(2)
            mock_subscribe.HOLD_API = False
            gevent.sleep(3)
        # end _execute_subscribe_concurrently

        # client1:subscribe(fields:X,Y) client2:subscribe(VN:fields X,Z)
        resources_query1 = {
            "virtual_network": {
                "fields": [
                    "routing_instances",
                    "id_perms"]}}
        resources_query2 = {
            "virtual_network": {
                "fields": [
                    "routing_instances",
                    "display_name"]}}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue_list = [client_queue1, client_queue2]
        watcher_resource_types = ["virtual_network"]
        _execute_subscribe_concurrently(
            client_queue_list, watcher_resource_types)
        expected_request_fields = [
            "routing_instances",
            "id_perms",
            "display_name",
            "id_perms"]
        for resource_type in watcher_resource_types:
            result_request_fields = self._dispatcher._get_watchers()[
                resource_type].obj_fields
            self.assertEquals(len(result_request_fields), 4)
            self.assertEquals(
                set(expected_request_fields),
                set(result_request_fields))
            self.assertEquals(set([client_queue1, client_queue2]), set(
                self._dispatcher._get_watchers()[resource_type].queues))
    # end test_concurrent_requests_subscribe_client_with_fields_xy_xz

    def test_concurrent_requests_subscribe_client_with_fields_all_xy(self):
        def _execute_subscribe_concurrently(
                client_queue_list, watcher_resource_types):
            mock_subscribe = MockSubscribeAndUnsubscribe()
            self._dispatcher._subscribe_client_queue = mock_subscribe.mock_subscribe_client_queue
            for client_queue in client_queue_list:
                gevent.spawn(
                    self._dispatcher.subscribe_client,
                    client_queue,
                    watcher_resource_types)
            gevent.sleep(2)
            mock_subscribe.HOLD_API = False
            gevent.sleep(3)
        # end _execute_subscribe_concurrently

        # Client1:subscribe(fields:all), Client2:subscribe(fields:X,Y)
        resources_query1 = {}
        resources_query2 = {
            "virtual_network": {
                "fields": [
                    "display_name",
                    "id_perms"]}}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue_list = [client_queue1, client_queue2]
        watcher_resource_types = ["virtual_network"]
        _execute_subscribe_concurrently(
            client_queue_list, watcher_resource_types)
        expected_request_fields = ["all", "display_name", "id_perms"]
        for resource_type in watcher_resource_types:
            result_request_fields = self._dispatcher._get_watchers()[
                resource_type].obj_fields
            self.assertEquals(len(result_request_fields), 3)
            self.assertEquals(
                set(expected_request_fields),
                set(result_request_fields))
            self.assertEquals(set([client_queue1, client_queue2]), set(
                self._dispatcher._get_watchers()[resource_type].queues))
    # end test_concurrent_requests_subscribe_client_with_fields_all_xy

    def test_concurrent_subscribe_and_unsubscribe_client_with_fields_all_xy(
            self):
        def _execute_subscribe_unsubscribe_concurrently(
                method_invocation_list, client_queue_list, watcher_resource_types):
            mock_obj = MockSubscribeAndUnsubscribe()
            self._dispatcher._subscribe_client_queue = mock_obj.mock_subscribe_client_queue
            self._dispatcher._unsubscribe_client_queue = mock_obj.mock_unsubscribe_client_queue
            for index, method in enumerate(method_invocation_list):
                gevent.spawn(
                    method,
                    client_queue_list[index],
                    watcher_resource_types)
            gevent.sleep(2)
            mock_obj.HOLD_API = False
            gevent.sleep(3)
        # end _execute_subscribe_unsubscribe_concurrently

        # Client1:unsubscribe(fields:all), Client2:subscribe(fields:X,Y)
        resources_query1 = {}
        resources_query2 = {
            "virtual_network": {
                "fields": [
                    "id_perms",
                    "display_name"]}}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue_list = [client_queue1, client_queue2]
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(
            client_queue1, watcher_resource_types)
        method_invocation_list = [
            self._dispatcher.unsubscribe_client,
            self._dispatcher.subscribe_client]
        _execute_subscribe_unsubscribe_concurrently(
            method_invocation_list, client_queue_list, watcher_resource_types)
        expected_request_fields = ["display_name", "id_perms"]
        for resource_type in watcher_resource_types:
            result_request_fields = self._dispatcher._get_watchers()[
                resource_type].obj_fields
            self.assertEquals(len(result_request_fields), 2)
            self.assertEquals(
                set(expected_request_fields),
                set(result_request_fields))
            self.assertEquals(
                [client_queue2],
                self._dispatcher._get_watchers()[resource_type].queues)
    # end test_concurrent_subscribe_and_unsubscribe_client_with_fields_all_xy

    def test_concurrent_subscribe_and_unsubscribe_client_with_fields_xy_xz(
            self):
        def _execute_subscribe_unsubscribe_concurrently(
                method_invocation_list, client_queue_list, watcher_resource_types):
            mock_obj = MockSubscribeAndUnsubscribe()
            self._dispatcher._subscribe_client_queue = mock_obj.mock_subscribe_client_queue
            self._dispatcher._unsubscribe_client_queue = mock_obj.mock_unsubscribe_client_queue
            for index, method in enumerate(method_invocation_list):
                gevent.spawn(
                    method,
                    client_queue_list[index],
                    watcher_resource_types)
            gevent.sleep(2)
            mock_obj.HOLD_API = False
            gevent.sleep(3)
        # end _execute_subscribe_unsubscribe_concurrently

        # Client1:subscribe(fields:X,Y), Client2:unsubscribe(fields:X,Z)
        resources_query1 = {
            "virtual_network": {
                "fields": [
                    "display_name",
                    "routing_instances"]}}
        resources_query2 = {
            "virtual_network": {
                "fields": [
                    "display_name",
                    "id_perms"]}}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue_list = [client_queue1, client_queue2]
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(
            client_queue2, watcher_resource_types)
        method_invocation_list = [
            self._dispatcher.subscribe_client,
            self._dispatcher.unsubscribe_client]
        _execute_subscribe_unsubscribe_concurrently(
            method_invocation_list, client_queue_list, watcher_resource_types)
        expected_request_fields = ["display_name", "routing_instances"]
        for resource_type in watcher_resource_types:
            result_request_fields = self._dispatcher._get_watchers()[
                resource_type].obj_fields
            self.assertEquals(len(result_request_fields), 2)
            self.assertEquals(
                set(expected_request_fields),
                set(result_request_fields))
            self.assertEquals(
                [client_queue1],
                self._dispatcher._get_watchers()[resource_type].queues)
    # end test_concurrent_subscribe_and_unsubscribe_client_with_fields_xy_xz

    def test_concurrent_requests_unsubscribe_client_with_fields_all(self):
        def _execute_unsubscribe_concurrently(
                client_queue_list, watcher_resource_types):
            mock_unsubscribe = MockSubscribeAndUnsubscribe()
            self._dispatcher._unsubscribe_client_queue = mock_unsubscribe.mock_unsubscribe_client_queue
            for client_queue in client_queue_list:
                gevent.spawn(
                    self._dispatcher.unsubscribe_client,
                    client_queue,
                    watcher_resource_types)
            gevent.sleep(2)
            mock_unsubscribe.HOLD_API = False
            gevent.sleep(3)
        # end _execute_unsubscribe_concurrently

        # client1:unsubscribe(fields: all) client2:unsubscribe(fields: all)
        resources_query1 = {}
        resources_query2 = {}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue_list = [client_queue1, client_queue2]
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(
            client_queue1, watcher_resource_types)
        self._dispatcher.subscribe_client(
            client_queue2, watcher_resource_types)
        _execute_unsubscribe_concurrently(
            client_queue_list, watcher_resource_types)
        for resource_type in watcher_resource_types:
            result_request_fields = self._dispatcher._get_watchers()[
                resource_type].obj_fields
            self.assertEquals(len(result_request_fields), 0)
            self.assertEquals([], result_request_fields)
            self.assertEquals([], self._dispatcher._get_watchers()[
                resource_type].queues)
    # end test_concurrent_requests_unsubscribe_client_with_fields_all

    def test_concurrent_requests_unsubscribe_client_with_fields_xy_xz(self):
        def _execute_unsubscribe_concurrently(
                client_queue_list, watcher_resource_types):
            mock_unsubscribe = MockSubscribeAndUnsubscribe()
            self._dispatcher._unsubscribe_client_queue = mock_unsubscribe.mock_unsubscribe_client_queue
            for client_queue in client_queue_list:
                gevent.spawn(
                    self._dispatcher.unsubscribe_client,
                    client_queue,
                    watcher_resource_types)
            gevent.sleep(2)
            mock_unsubscribe.HOLD_API = False
            gevent.sleep(3)
        # end _execute_unsubscribe_concurrently

        # client1:unsubscribe(fields:X,Y), client2:unsubscribe(fields:X,Z)
        resources_query1 = {
            "virtual_network": {
                "fields": [
                    "display_name",
                    "routing_instances"]}}
        resources_query2 = {
            "virtual_network": {
                "fields": [
                    "display_name",
                    "id_perms"]}}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue_list = [client_queue1, client_queue2]
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(
            client_queue1, watcher_resource_types)
        self._dispatcher.subscribe_client(
            client_queue2, watcher_resource_types)
        _execute_unsubscribe_concurrently(
            client_queue_list, watcher_resource_types)
        for resource_type in watcher_resource_types:
            result_request_fields = self._dispatcher._get_watchers()[
                resource_type].obj_fields
            self.assertEquals(len(result_request_fields), 0)
            self.assertEquals([], result_request_fields)
            self.assertEquals([], self._dispatcher._get_watchers()[
                resource_type].queues)
    # end test_concurrent_requests_unsubscribe_client_with_fields_xy_xz

    def test_subscribe_client(self):
        resources_query1 = {
            "virtual_network": {
                "fields": [
                    "routing_instances",
                    "id_perms"]}}
        resources_query2 = {}
        resources_query3 = {
            "virtual_network": {
                "fields": ["display_name"]}}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue3 = WatcherQueue(resources_query3)
        watcher_resource_types = ["virtual_network"]
        gevent.joinall(
            [
                gevent.spawn(
                    self._dispatcher.subscribe_client(
                        client_queue1, watcher_resource_types)), gevent.spawn(
                    self._dispatcher.subscribe_client(
                        client_queue2, watcher_resource_types)), gevent.spawn(
                    self._dispatcher.subscribe_client(
                        client_queue3, watcher_resource_types))])

        for resource_type in watcher_resource_types:
            self.assertEquals(set([client_queue1, client_queue2, client_queue3]), set(
                self._dispatcher._get_watchers().get(resource_type).queues))
            self.assertEquals(set(["routing_instances", "id_perms", "display_name", "all"]), set(
                self._dispatcher._get_watchers().get(resource_type).obj_fields))
    # end test_subscribe_client

    def test_unsubscribe_client(self):
        resources_query1 = {
            "virtual_network": {
                "fields": [
                    "routing_instances",
                    "id_perms"]}}
        resources_query2 = {}
        resources_query3 = {}

        client_queue1 = WatcherQueue(resources_query1)
        client_queue2 = WatcherQueue(resources_query2)
        client_queue3 = WatcherQueue(resources_query3)
        watcher_resource_types = [
            "virtual_network"]
        self._dispatcher.subscribe_client(
            client_queue1, watcher_resource_types)
        self._dispatcher.subscribe_client(
            client_queue2, watcher_resource_types)
        self._dispatcher.subscribe_client(
            client_queue3, watcher_resource_types)

        gevent.joinall(
            [
                gevent.spawn(
                    self._dispatcher.unsubscribe_client(
                        client_queue2, watcher_resource_types)), gevent.spawn(
                    self._dispatcher.unsubscribe_client(
                        client_queue3, watcher_resource_types))])
        for resource_type in watcher_resource_types:
            self.assertEquals(set([client_queue1]), set(
                self._dispatcher._get_watchers().get(resource_type).queues))
            self.assertEquals(set(["routing_instances", "id_perms"]), set(
                self._dispatcher._get_watchers().get(resource_type).obj_fields))
    # end test_unsubscribe_client


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
        self._dispatcher._set_watchers({})
        self._dispatcher._set_db_conn(None)
        super(TestDispatch, self).tearDown()

    def notify(self, notifications):
        for notification in notifications:
            self._dispatcher.notify_event_dispatcher(notification)

    def test_dispatch_with_field_query(self):
        vm_resp = {
            'uuid': '1a9678c5',
            'fq_name': ['default-domain'],
            'parent_type': 'project',
            'parent_uuid': '71d08380',
            'display_name': {},
            'routing_instances': {}}
        mockVncDBClient = flexmock(
            dbe_read=lambda resource_type,
            resource_id,
            obj_fields: (
                True,
                vm_resp))
        self._dispatcher._set_db_conn(mockVncDBClient)
        notifications = [
            {
                "oper": "CREATE",
                "type": "virtual_network_interface",
                "uuid": "123"
            },
            {
                "oper": "CREATE",
                "type": "virtual_network",
                "uuid": "123"
            },
            {
                "oper": "DELETE",
                "type": "virtual_network",
                "uuid": "123"
            },
        ]
        resources_query = {
            "virtual_network": {
                "fields": [
                    "display_name",
                    "routing_instances"]}}

        client_queue = WatcherQueue(resources_query)
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)

        for notification in notifications:
            if notification['type'] == "virtual_network_interface":
                continue
            event = {
                "event": notification.get("oper").lower(),
                "data": json.dumps({"virtual-network": vm_resp})
            }
            if notification.get("oper") == "DELETE":
                event.update(
                    {
                        "data": json.dumps(
                            {"virtual-network": {"uuid": "123"}}
                        )
                    }
                )
            self.assertEquals(
                event,
                client_queue.get()
            )

    def test_dispatch_no_extra_read(self):
        mockVncDBClient = flexmock(
            dbe_read=lambda resource_type, resource_id: (True, {}))
        self._dispatcher._set_db_conn(mockVncDBClient)
        notifications = [
            {
                "oper": "CREATE",
                "type": "virtual_network_interface",
                "uuid": "123"
            },
            {
                "oper": "CREATE",
                "type": "virtual_network",
                "uuid": "123"
            },
            {
                "oper": "DELETE",
                "type": "virtual_network",
                "uuid": "123"
            },
        ]

        client_queue = WatcherQueue({})
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            if notification['type'] == "virtual_network_interface":
                continue
            event = {
                "event": notification.get("oper").lower(),
                "data": json.dumps({"virtual-network": {}})
            }
            if notification.get("oper") == "DELETE":
                event.update(
                    {
                        "data": json.dumps(
                            {"virtual-network": {"uuid": "123"}}
                        )
                    }
                )
            self.assertEquals(
                event,
                client_queue.get()
            )

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

        client_queue = WatcherQueue({})
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {
                "event": notification.get("oper").lower(),
                "data": json.dumps({"virtual-network": {}})
            }
            if notification.get("oper") == "DELETE":
                event.update(
                    {
                        "data": json.dumps(
                            {"virtual-network": {"uuid": "123"}}
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

        client_queue = WatcherQueue({})
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {"event": "stop", "data": json.dumps(
                {"virtual-network": {
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
        mockVncDBClient.should_receive('config_log').and_return()
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

        client_queue = WatcherQueue({})
        watcher_resource_types = ["virtual_network"]
        self._dispatcher.subscribe_client(client_queue, watcher_resource_types)
        gevent.spawn(self.notify, notifications).join()
        gevent.spawn(self._dispatcher.dispatch)
        for notification in notifications:
            event = {
                "event": "stop",
                "data": json.dumps({
                    "virtual-network": {
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

    def test_initialize_with_fields_query(self):
        vmi_resp = [
            {"virtail-machine-interface": {
                "instance_ip_back_refs": {},
                "virtaul_machine_interface_bindings": {}}
             }]
        mockVncDBClient = flexmock(
            dbe_list=lambda resource_type, field_names: (True, [vmi_resp], 0)
        )
        self._dispatcher._set_db_conn(mockVncDBClient)
        watcher_resource_types = [
            "virtual_machine_interface"]
        resource_query = {
            "fields": "instance_ip_back_refs,virtaul_machine_interface_bindings"}
        for resource_type in watcher_resource_types:
            object_type = resource_type.replace("_", "-")
            self.assertEqual(
                (
                    True,
                    {
                        "event": "init",
                        "data": json.dumps(
                            {
                                object_type + 's': [{object_type: vmi_resp}]
                            }
                        )
                    }
                ),
                self._dispatcher.initialize(resource_type, resource_query)
            )

    def test_initialize_no_dbe_list_failure_no_exception(self):
        mockVncDBClient = flexmock(
            dbe_list=lambda resource_type, is_detail: (True, [{}], 0)
        )
        self._dispatcher._set_db_conn(mockVncDBClient)
        watcher_resource_types = [
            "virtual_network",
            "virtual_network_interface"]
        for resource_type in watcher_resource_types:
            object_type = resource_type.replace("_", "-")
            self.assertEqual(
                (
                    True,
                    {
                        "event": "init",
                        "data": json.dumps(
                            {
                                object_type + 's': [{object_type: {}}]
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
            object_type = resource_type.replace("_", "-")
            self.assertEqual(
                (
                    False,
                    {
                        "event": "stop",
                        "data": json.dumps(
                            {
                                object_type + 's': "Mock list failure"
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
            object_type = resource_type.replace("_", "-")
            self.assertEqual(
                (
                    False,
                    {
                        "event": "stop",
                        "data": json.dumps(
                            {
                                object_type + 's': [
                                    {
                                        object_type: {
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
