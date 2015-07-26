import sys
import time
sys.path.append("../config/common/tests")
from test_utils import *
import fixtures
import testtools
import test_common
import test_discovery

import discoveryclient.client as client

def info_callback(info):
    # print 'In subscribe callback handler'
    # print '%s' % (info)
    pass


class DiscoveryServerTestCase(test_discovery.TestCase, fixtures.TestWithFixtures):
    def test_load_balance(self):
        # publish 3 instances of service foobar
        tasks = []
        service_type = 'foobar'
        for i in range(3):
            client_type = 'test-discovery'
            pub_id = 'test_discovery-%d' % i
            pub_data = '%s-%d' % ('foobar', i)
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port,
                        client_type, pub_id)
            task = disc.publish(service_type, pub_data)
            tasks.append(task)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), 3)
        self.assertEqual(response['services'][0]['service_type'], 'foobar')

        # multiple subscribers for 2 instances each
        subcount = 20
        service_count = 2
        tasks = []
        for i in range(subcount):
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port,
                        "test-load-balance-%d" % i)
            obj = disc.subscribe(
                      service_type, service_count, info_callback)
            tasks.append(obj.task)
            time.sleep(1)
        print 'Started %d tasks to subscribe service %s, count %d' \
            % (subcount, service_type, service_count)

        # validate all clients have subscribed
        time.sleep(1)
        (code, msg) = self._http_get('/clients.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), subcount*service_count)

        # start one more publisher
        pub_id = 'test_discovery-3'
        pub_data = 'foobar-3'
        disc = client.DiscoveryClient(
                    self._disc_server_ip, self._disc_server_port,
                    client_type, pub_id)
        task = disc.publish(service_type, pub_data)
        tasks.append(task)

        # verify 4th publisher is up
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 4)
        print response

        # wait for all TTL to expire before looking at publisher's counters
        print 'Waiting for all client TTL to expire (1 min)'
        time.sleep(1*60)

        # total subscriptions (must be subscount * service_count)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        subs = sum([item['in_use'] for item in response['services']])
        self.assertEqual(subs, subcount*service_count)

        # verify newly added in-use count is 0
        data = [item for item in response['services'] if item['service_id'] == 'test_discovery-3:foobar']
        entry = data[0]
        self.assertEqual(len(data), 1)
        self.assertEqual(entry['in_use'], 0)

        # Issue load-balance command
        (code, msg) = self._http_post('/load-balance/foobar', '')
        self.assertEqual(code, 200)

        # wait for all TTL to expire before looking at publisher's counters
        print 'Waiting for all client TTL to expire (1 min)'
        time.sleep(1*60)
        pass

        # total subscriptions (must still be subscount * service_count)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        subs = sum([item['in_use'] for item in response['services']])
        self.assertEqual(subs, subcount*service_count)

        # verify newly added in-use count is 10
        data = [item for item in response['services'] if item['service_id'] == 'test_discovery-3:foobar']
        entry = data[0]
        self.assertEqual(len(data), 1)
        print 'After LB entry %s' % entry
        self.assertEqual(entry['in_use'], 10)
