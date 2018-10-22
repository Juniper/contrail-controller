from collections import deque
import unittest

from freezegun import freeze_time

from route_update import IPRouteWatcher, RouteUpdate


class RouteUpdateMock:
    def __init__(self, update):
        self.update = update

    def get_attr(self, key):
        return self.update.get(key)

    def __getitem__(self, key):
        return self.update[key]


class TestIPRouteWatcher(unittest.TestCase):
    def test_creation(self):
        iprw = IPRouteWatcher()

        self.assertIsInstance(iprw.messages, deque)

    def test_bad_process_update(self):
        update = RouteUpdateMock({
            'event': 'ASD',
            'dst_len': 11,
            'RTA_DST': '1.1.1.1',
            'RTA_OIF': 1
        })

        ret = IPRouteWatcher.process_update(update)

        self.assertEqual(ret, None)

    @freeze_time("2012-04-01")
    def test_add_process_update(self):
        update = RouteUpdateMock({
            'event': 'RTM_NEWROUTE',
            'dst_len': 11,
            'RTA_DST': '1.1.1.1',
            'RTA_OIF': 1
        })

        ret = IPRouteWatcher.process_update(update)

        exp = RouteUpdate(
            timestamp=1333238400,  # 04/01/2012 @ 12:00am (UTC)
            operation='Add',
            target='1.1.1.1/11',
            via=None,
            interface='asd')

        self.assertListEqual(
            [ret.timestamp, ret.operation, ret.target, ret.via],
            [exp.timestamp, exp.operation, exp.target, exp.via])

    @freeze_time("2013-04-01")
    def test_delete_process_update(self):
        update = RouteUpdateMock({
            'event': 'RTM_DELROUTE',
            'dst_len': 21,
            'RTA_DST': '2.1.2.1',
            'RTA_GATEWAY': '33.22.11.99',
            'RTA_OIF': 1
        })

        ret = IPRouteWatcher.process_update(update)

        exp = RouteUpdate(
            timestamp=1364774400,  # 04/01/2012 @ 12:00am (UTC)
            operation='Delete',
            target='2.1.2.1/21',
            via='33.22.11.99',
            interface='asd')

        self.assertListEqual(
            [ret.timestamp, ret.operation, ret.target, ret.via],
            [exp.timestamp, exp.operation, exp.target, exp.via])


class TestRouteUpdate(unittest.TestCase):
    def test_creation(self):
        RouteUpdate(1111, 'add', '11.1.1.1', None, 'eth0')
