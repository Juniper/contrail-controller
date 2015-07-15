import argparse
import bottle
import mock
import time
import unittest
import uuid
import json


from discovery import disc_consts
from discovery.disc_server import DiscoveryServer


def parse_args():
    defaults = {
        'reset_config': False,
        'listen_ip_addr': disc_consts._WEB_HOST,
        'listen_port': disc_consts._WEB_PORT,
        'cass_server_ip': disc_consts._CASSANDRA_HOST,
        'cass_server_port': disc_consts._CASSANDRA_PORT,
        'ttl_min': disc_consts._TTL_MIN,
        'ttl_max': disc_consts._TTL_MAX,
        'ttl_short': 0,
        'hc_interval': disc_consts.HC_INTERVAL,
        'hc_max_miss': disc_consts.HC_MAX_MISS,
        'collectors': None,
        'http_server_port': '5997',
        'log_local': False,
        'log_level': 1,
        'log_category': '',
        'log_file': '/tmp/log',
        'worker_id': '0',
        'logger_class': None,
        'logging_conf': '',
    }

    parser = argparse.ArgumentParser()
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')

    args = parser.parse_args("")

    args.service_config = {}

    return args


class FakeBottleRequest(object):

    def __init__(self, service='IfmapServer', client='aaa', instances=2):
        self._service = service
        self._instances = instances
        self._client = client

    @property
    def headers(self):
        return {'content-type': 'application/json'}

    @property
    def json(self):
        return {'service': self._service,
                'client': self._client,
                'instances': self._instances}

    @property
    def environ(self):
        return {'REMOTE_ADDR': '127.0.0.1'}


class FakeCassandraClient(object):

    def __init__(self):
        self.service_entries = {}
        self.type_entries = {}

    def subscriber_entries(self):
        return []

    def delete_subscription(self, service_type, client_id, service_id):
        if service_type not in self.type_entries:
            return
        col_name = '/'.join(['subscriber', service_id, client_id])
        self.type_entries[service_type].pop(col_name, None)

        col_name = '/'.join(['client', client_id, service_id])
        self.type_entries[service_type].pop(col_name, None)

    def insert_client(self, service_type, service_id, client_id, blob, ttl):
        if service_type not in self.type_entries:
            self.type_entries[service_type] = {}

        col_val = {'ttl': ttl, 'blob': blob,
                              'mtime': int(time.time())}
        col_name = '/'.join(['subscriber', service_id, client_id])
        self.type_entries[service_type][col_name] = (col_val, time.time())

        col_name = '/'.join(['client', client_id, service_id])
        self.type_entries[service_type][col_name] = (col_val, time.time())

    def lookup_client(self, service_type, client_id):
        if service_type not in self.type_entries:
            return (None, [])
        cols = []
        subs = sorted(self.type_entries[service_type].items(),
                      key=lambda entry: entry[1][1])
        data = None
        r = []
        for col_name, col_val in subs:
            col_type, cl_id, service_id = col_name.split('/')
            if col_type != 'client' or cl_id != client_id:
                continue
            if service_id == disc_consts.CLIENT_TAG:
                data = col_val[0]
                continue
            entry = col_val[0]
            r.append((service_id, entry['blob']))
        return (data, r)

    def insert_client_data(self, service_type, client_id, blob):
        if service_type not in self.type_entries:
            self.type_entries[service_type] = {}
        col_name = '/'.join(['client', client_id, disc_consts.CLIENT_TAG])
        self.type_entries[service_type][col_name] = (blob, time.time())

    def lookup_service(self, service_type, service_id=None):
        services = self.service_entries.get(service_type)
        if not services:
            return []

        if service_id:
            return services[service_id]

        return services.values()

    def update_service(self, service_type, sig, entry):
        services = self.service_entries.get(service_type, {})
        services[sig] = entry
        self.service_entries[service_type] = services

    def insert_service(self, service_type, sig, entry):
        self.update_service(service_type, sig, entry)

    def mark_service_as_fail(self, service_type, service_id):
        services = self.service_entries.get(service_type)
        if not services:
            return

        service = services.get(service_id)
        if service:
            service['heartbeat'] = 0


class PolicyTest(unittest.TestCase):

    def setUp(self):
        args = parse_args()

        self.fake_db = FakeCassandraClient()
        self.db_conn_patch = mock.patch(('discovery.disc_server.'
                                         'DiscoveryServer._db_connect'),
                                        return_value=self.fake_db)
        self.db_conn_patch.start()
        self.syslog_patch = mock.patch(('discovery.disc_server.'
                                        'DiscoveryServer.syslog'))

        self.syslog_patch.start()
        self.server = DiscoveryServer(args)

    def tearDown(self):
        self.db_conn_patch.stop()
        self.syslog_patch.stop()

    def insert_fake_services(self):
        t = time.time() - 1
        self.fake_db.insert_service('IfmapServer', 'if1',
                                    {'service_id': 'if1',
                                     'heartbeat': t,
                                     'admin_state': 'up',
                                     'ts_use': 0,
                                     'info': {'ip-address': '1.1.1.1',
                                              'port': u'8443'}})
        self.fake_db.insert_service('IfmapServer', 'if2',
                                    {'service_id': 'if2',
                                     'heartbeat': t,
                                     'admin_state': 'up',
                                     'ts_use': 0,
                                     'info': {'ip-address': '2.2.2.2',
                                              'port': u'8443'}})
        self.fake_db.insert_service('IfmapServer', 'if3',
                                    {'service_id': 'if3',
                                     'heartbeat': t,
                                     'admin_state': 'up',
                                     'ts_use': 0,
                                     'info': {'ip-address': '3.3.3.3',
                                              'port': u'8443'}})


class LoadBalancingPolicyTest(PolicyTest):

    # test that we get the less used each time a new client
    # is subscribing
    def test_one_instance_multiple_clients(self):
        self.insert_fake_services()
        bottle.request = FakeBottleRequest(instances=1)

        with mock.patch.object(self.server, 'get_service_config',
                               return_value="load-balancing"):
            expected = ['1.1.1.1', '2.2.2.2', '3.3.3.3', '1.1.1.1']
            for expt in expected:
                bottle.request = FakeBottleRequest(instances=1,
                                                   client=str(uuid.uuid4()))
                res = self.server.api_subscribe()
                self.assertEquals(res['IfmapServer'][0]['ip-address'], expt)

    # test that a client always get the same service that it suscribed
    # previously
    def test_one_instance_one_client(self):
        self.insert_fake_services()
        bottle.request = FakeBottleRequest(instances=1)

        with mock.patch.object(self.server, 'get_service_config',
                               return_value="load-balancing"):
            client_id = str(uuid.uuid4())
            bottle.request = FakeBottleRequest(instances=1,
                                               client=client_id)
            res = self.server.api_subscribe()
            expected = res['IfmapServer'][0]['ip-address']
            for i in range(10):
                res = self.server.api_subscribe()
                self.assertEquals(res['IfmapServer'][0]['ip-address'], expected)

    # test that a client keep the previously subscribed service plus another one
    # when requesting 1 more instance
    def test_adding_instance_one_client(self):
        self.insert_fake_services()
        bottle.request = FakeBottleRequest(instances=1)

        with mock.patch.object(self.server, 'get_service_config',
                               return_value="load-balancing"):
            client_id = str(uuid.uuid4())
            bottle.request = FakeBottleRequest(instances=1,
                                               client=client_id)
            res = self.server.api_subscribe()
            expected = res['IfmapServer'][0]['ip-address']

            bottle.request = FakeBottleRequest(instances=2,
                                               client=client_id)
            for i in range(10):
                res = self.server.api_subscribe()
                self.assertEquals(res['IfmapServer'][0]['ip-address'], expected)
                self.assertIsNotNone(res['IfmapServer'][1]['ip-address'])
                self.assertNotEquals(res['IfmapServer'][0]['ip-address'],
                                     res['IfmapServer'][1]['ip-address'])

    def test_two_instances_one_client_one_service_failing(self):
        self.insert_fake_services()
        bottle.request = FakeBottleRequest(instances=1)

        with mock.patch.object(self.server, 'get_service_config',
                               return_value="load-balancing"):
            client_id = str(uuid.uuid4())
            bottle.request = FakeBottleRequest(instances=2,
                                               client=client_id)
            res = self.server.api_subscribe()
            self.assertEquals(len(res['IfmapServer']), 2)
            invalid = res['IfmapServer'][0]['ip-address']
            valid = res['IfmapServer'][1]['ip-address']

            services = self.fake_db.lookup_service('IfmapServer')

            service_id = None
            for service in services:
                if service['info']['ip-address'] == invalid:
                    service_id = service['service_id']
                    break
            self.assertIsNotNone(service_id)

            self.fake_db.mark_service_as_fail('IfmapServer', service_id)

            res = self.server.api_subscribe()
            ips = [service['ip-address'] for service in res['IfmapServer']]
            self.assertNotIn(invalid, ips)
            self.assertIn(valid, ips)


class RoundRobinPolicyTest(PolicyTest):
    pass
