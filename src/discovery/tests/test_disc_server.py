import argparse
import mock
import time
import unittest

import bottle

from discovery import disc_consts
from discovery.disc_server import DiscoveryServer

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


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
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'worker_id': '0',
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

    def __init__(self, service='IfmapServer', instances=2):
        self.service = service
        self.instances = instances

    @property
    def headers(self):
        return {'content-type': 'application/json'}

    @property
    def json(self):
        return {'service': self.service,
                'client': 'aaa',
                'instances': self.instances}

    @property
    def environ(self):
        return {'REMOTE_ADDR': '127.0.0.1'}


class FakeCassandraClient(object):

    def __init__(self):
        self.service_entries = {}

    def subscriber_entries(self):
        return []

    def insert_client(self, service_type, service_id, client_id, result, ttl):
        pass

    def lookup_client(self, service_type, client_id):
        return (None, [])

    def insert_client_data(self, service_type, client_id, cl_entry):
        pass

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

    def test_round_robin(self):
        t = time.time() - 5
        self.fake_db.insert_service('IfmapServer', 'if1',
                                    {'service_id': 'if1',
                                     'heartbeat': t,
                                     'admin_state': 'up',
                                     'ts_use': 1,
                                     'info': {'ip-address': '1.1.1.1',
                                              'port': u'8443'}})
        self.fake_db.insert_service('IfmapServer', 'if2',
                                    {'service_id': 'if2',
                                     'heartbeat': t,
                                     'admin_state': 'up',
                                     'ts_use': 2,
                                     'info': {'ip-address': '2.2.2.2',
                                              'port': u'8443'}})

        bottle.request = FakeBottleRequest()

        with mock.patch.object(self.server, 'get_service_config',
                               return_value="round-robin"):
            res = self.server.api_subscribe()
            self.assertEquals(res['IfmapServer'][0]['ip-address'], '2.2.2.2')
            self.assertEquals(res['IfmapServer'][1]['ip-address'], '1.1.1.1')

            res = self.server.api_subscribe()
            self.assertEquals(res['IfmapServer'][0]['ip-address'], '1.1.1.1')
            self.assertEquals(res['IfmapServer'][1]['ip-address'], '2.2.2.2')
