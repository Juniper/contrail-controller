import sys
import time
sys.path.append("../config/common/tests")
from test_utils import *
import fixtures
import testtools
import test_common
import test_case

import discoveryclient.client as client

def info_callback(info, client_id):
    # print 'In subscribe callback handler'
    print 'client-id %s info %s' % (client_id, info)
    pass

"""
Validate publisher in-use count is reasonable (typically
after load-balance event. Discovery server will try to keep
in-use count with 5% of expected average. To provide some
buffer around server calculations, we allow 10% deviation
"""
def validate_assignment_count(response, context):
    services = response['services']
    in_use_counts = {entry['service_id']:entry['in_use'] for entry in services}
    print '%s %s' % (context, in_use_counts)

    # only use active pubs
    pubs_active = [entry for entry in services if entry['status'] != 'down']

    # validate
    avg = sum([entry['in_use'] for entry in pubs_active])/len(pubs_active)

    # return failure status
    return True in [e['in_use'] > int(1.1*avg) for e in pubs_active]

class DiscoveryServerTestCase(test_case.DsTestCase):
    def setUp(self):
        extra_config_knobs = [
            ('SvcActiveLoadBalance', 'load-balance', 'True'),
        ]
        super(DiscoveryServerTestCase, self).setUp(extra_disc_server_config_knobs=extra_config_knobs)

    def test_load_balance(self):
        # publish 3 instances
        tasks = []
        service_type = 'SvcLoadBalance'
        for i in range(3):
            client_type = 'test-discovery'
            pub_id = 'test_discovery-%d' % i
            pub_data = {service_type : '%s-%d' % (service_type, i)}
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
        self.assertEqual(response['services'][0]['service_type'], service_type)

        # multiple subscribers for 2 instances each
        subcount = 20
        service_count = 2
        tasks = []
        for i in range(subcount):
            client_id = "test-load-balance-%d" % i
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port, client_id)
            obj = disc.subscribe(
                      service_type, service_count, info_callback, client_id)
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
        pub_data = {service_type : '%s-3' % service_type}
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
        data = [item for item in response['services'] if item['service_id'] == 'test_discovery-3:%s' % service_type]
        entry = data[0]
        self.assertEqual(len(data), 1)
        self.assertEqual(entry['in_use'], 0)

        # Issue load-balance command
        (code, msg) = self._http_post('/load-balance/%s' % service_type, '')
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
        data = [item for item in response['services'] if item['service_id'] == 'test_discovery-3:%s' % service_type]
        entry = data[0]
        self.assertEqual(len(data), 1)
        print 'After LB entry %s' % entry
        self.assertEqual(entry['in_use'], 10)

    def test_active_load_balance(self):
        # publish 3 instances of service. Active LB must be enabled!
        tasks = []
        service_type = 'SvcActiveLoadBalance'
        for i in range(3):
            client_type = 'test-discovery'
            pub_id = 'test_discovery-%d' % i
            pub_data = {service_type : '%s-%d' % (service_type, i)}
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
        self.assertEqual(response['services'][0]['service_type'], service_type)
        failure = validate_assignment_count(response, 'In-use count just after publishing')
        self.assertEqual(failure, False)

        # multiple subscribers for 2 instances each
        subcount = 20
        service_count = 2
        tasks = []
        for i in range(subcount):
            client_id = "test-load-balance-%d" % i
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port, client_id)
            obj = disc.subscribe(
                      service_type, service_count, info_callback, client_id)
            tasks.append(obj.task)
            time.sleep(1)

        # validate all clients have subscribed
        time.sleep(1)
        (code, msg) = self._http_get('/clients.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), subcount*service_count)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        failure = validate_assignment_count(response, 'In-use count just after initial subscribe')
        self.assertEqual(failure, False)

        # start one more publisher
        pub_id = 'test_discovery-3'
        pub_data = {service_type : '%s-3' % service_type}
        pub_url = '/service/%s' % pub_id
        disc = client.DiscoveryClient(
                    self._disc_server_ip, self._disc_server_port,
                    client_type, pub_id)
        task = disc.publish(service_type, pub_data)
        tasks.append(task)

        # wait for all TTL to expire before looking at publisher's counters
        print 'Waiting for all client TTL to expire (1 min)'
        time.sleep(1*60)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        failure = validate_assignment_count(response, 'In-use count just after bringing up one more publisher')
        self.assertEqual(failure, False)

        # set operational state down - new service
        payload = {
            'service-type' : '%s' % service_type,
            'oper-state'   : 'down',
        }
        (code, msg) = self._http_put(pub_url, json.dumps(payload))
        self.assertEqual(code, 200)

        # wait for all TTL to expire before looking at publisher's counters
        print 'Waiting for all client TTL to expire (1 min)'
        time.sleep(1*60)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        failure = validate_assignment_count(response, 'In-use count just after publisher-3 down')
        self.assertEqual(failure, False)

        # set operational state up - again
        payload = {
            'service-type' : '%s' % service_type,
            'oper-state'   : 'up',
        }
        (code, msg) = self._http_put(pub_url, json.dumps(payload))
        self.assertEqual(code, 200)

        # wait for all TTL to expire before looking at publisher's counters
        print 'Waiting for all client TTL to expire (1 min)'
        time.sleep(1*60)

        # total subscriptions must be subscount * service_count
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        failure = validate_assignment_count(response, 'In-use count just after publisher-3 up again')
        self.assertEqual(failure, False)

    def test_load_balance_min_instances(self):
        # publish 3 instances
        tasks = []
        service_type = 'Foobar'
        pubcount =  5
        for i in range(pubcount):
            client_type = 'test-discovery'
            pub_id = 'test_discovery-%d' % i
            pub_data = {service_type : '%s-%d' % (service_type, i)}
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port,
                        client_type, pub_id)
            task = disc.publish(service_type, pub_data)
            tasks.append(task)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), pubcount)
        self.assertEqual(response['services'][0]['service_type'], service_type)

        # multiple subscribers for 2 instances each
        subcount = 100
        min_instances = 2
        suburl = "/subscribe"
        payload = {
            'service'      : '%s' % service_type,
            'instances'    : 0,
            'min-instances': min_instances,
            'client-type'  : 'Vrouter-Agent',
            'remote-addr'  : '3.3.3.3',
            'version'      : '2.2',
        }
        for i in range(subcount):
            payload['client'] = "test-load-balance-%d" % i
            (code, msg) = self._http_post(suburl, json.dumps(payload))
            self.assertEqual(code, 200)
            response = json.loads(msg)
            self.assertEqual(len(response[service_type]), pubcount)

        # validate all clients have subscribed
        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        failure = validate_assignment_count(response, 'In-use count after clients with min_instances 2')
        self.assertEqual(failure, False)
