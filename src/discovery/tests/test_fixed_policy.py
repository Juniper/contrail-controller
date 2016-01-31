import sys
import time
sys.path.append("../config/common/tests")
from test_utils import *
import fixtures
import testtools
import test_common
import test_case

import discoveryclient.client as client

server_list = {}

class DiscoveryServerTestCase(test_case.DsTestCase):
    def setUp(self):
        extra_config_knobs = [
            ('foobar', 'policy', 'fixed'),
        ]
        super(DiscoveryServerTestCase, self).setUp(extra_disc_server_config_knobs=extra_config_knobs)

    def info_callback(self, info, client_id):
        global server_list
        print 'In subscribe callback handler for client %s' % client_id
        print '%s' % (info)
        # [{u'@publisher-id': 'test_discovery-0', u'foobar': 'foobar-0'}, {u'@publisher-id': 'test_discovery-1', u'foobar': 'foobar-1'}]
        server_list[client_id] = [entry['@publisher-id'] for entry in info]
        pass

    def test_fixed_policy(self):
        global server_list
        # publish 3 instances of service foobar
        tasks = []
        service_type = 'foobar'
        for i in range(3):
            client_type = 'test-discovery'
            pub_id = 'test_discovery-%d' % i
            pub_data = {service_type : '%s-%d' % ('foobar', i)}
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port,
                        client_type, pub_id)
            task = disc.publish(service_type, pub_data)
            tasks.append(task)
            time.sleep(1)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), 3)
        self.assertEqual(response['services'][0]['service_type'], 'foobar')

        # multiple subscribers for 2 instances each
        subcount = 3
        service_count = 2
        tasks = []
        for i in range(subcount):
            client_id = "test-fixed-policy-%d" % i
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port,
                        client_id, pub_id = client_id)
            obj = disc.subscribe(
                      service_type, service_count, self.info_callback, client_id)
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

        # validate all three clients have the same two servers ... 0 and 1
        for cid, slist in server_list.items():
            self.assertEqual(slist[0], 'test_discovery-0')
            self.assertEqual(slist[1], 'test_discovery-1')
        print 'All clients got the same servers in correct order'

        # mark server 1 down (foobar-1)
        puburl = '/service/test_discovery-1'
        payload = {
            'service-type'  : 'foobar',
            'oper-state'    : 'down',
        }
        (code, msg) = self._http_put(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 3)

        for entry in response['services']:
            if entry['service_id'] == 'test_discovery-1:foobar':
                break
        self.assertEqual(entry['oper_state'], 'down')

        # wait for max TTL to expire
        time.sleep(60)

        # validate all clients have subscribed to foobar-0 and foobar-2
        for cid, slist in server_list.items():
            self.assertEqual(slist[0], 'test_discovery-0')
            self.assertEqual(slist[1], 'test_discovery-2')
        print 'All clients got the same servers in correct order'

        # bring up foobar-1 server
        payload['oper-state'] = 'up'
        (code, msg) = self._http_put(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        # wait for max TTL to expire
        time.sleep(60)

        # validate all clients still subscribed to foobar-0 and foobar-2
        for cid, slist in server_list.items():
            self.assertEqual(slist[0], 'test_discovery-0')
            self.assertEqual(slist[1], 'test_discovery-2')
        print 'All clients got the same servers in correct order'


        # start one more client which should also get foobar-0 and foobar-2
        client_id = "test-fixed-policy-3"
        disc = client.DiscoveryClient(
                    self._disc_server_ip, self._disc_server_port,
                    client_id, pub_id = client_id)
        obj = disc.subscribe(
                  service_type, service_count, self.info_callback, client_id)
        tasks.append(obj.task)
        time.sleep(1)
        print 'Started 1 tasks to subscribe service %s' % service_type

        # validate all clients have subscribed
        time.sleep(1)
        (code, msg) = self._http_get('/clients.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 4*2)

        # validate all four clients have the same two servers ... 0 and 2
        for cid, slist in server_list.items():
            self.assertEqual(slist[0], 'test_discovery-0')
            self.assertEqual(slist[1], 'test_discovery-2')
        print 'All clients got the same servers in correct order'
