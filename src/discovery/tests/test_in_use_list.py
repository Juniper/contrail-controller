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

class DiscoveryServerTestCase(test_case.DsTestCase):

    def test_in_use_list(self):
        # publish 3 instances of service foobar
        tasks = []
        service_type = 'foobar'
        num_publishers = 5
        for i in range(num_publishers):
            pub_id = 'test_discovery-%d' % i
            pub_data = {'data': '%s-%d' % ('foobar', i)}
            disc = client.DiscoveryClient(
                        self._disc_server_ip, self._disc_server_port,
                        'test-discovery', pub_id)
            task = disc.publish(service_type, pub_data)
            tasks.append(task)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), num_publishers)
        for entry in response['services']:
            self.assertEqual(entry['service_type'], 'foobar')

        suburl = "/subscribe"
        payload = {
            'service'     : '%s' % service_type,
            'client'      : 'test-discovery',
            'instances'   : 0,
            'client-type' : 'test-discovery',
        }

        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 5)

        expected_response = set(['test_discovery-0','test_discovery-1','test_discovery-2','test_discovery-3','test_discovery-4'])
        rcvd_list = [entry['@publisher-id'] for entry in response[service_type]]
        self.assertEqual(expected_response, set(rcvd_list))

        # resend subscribe request with in-use set
        in_use_list = [rcvd_list[2], rcvd_list[3]]
        payload['service-in-use-list'] = {'publisher-id' : in_use_list}

        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 5)
        expected_response = set(['test_discovery-0','test_discovery-1','test_discovery-2','test_discovery-3','test_discovery-4'])
        rcvd_list = [entry['@publisher-id'] for entry in response[service_type]]
        self.assertEqual(expected_response, set(rcvd_list))

        self.assertEqual(rcvd_list[0], in_use_list[0])
        self.assertEqual(rcvd_list[1], in_use_list[1])

    def test_publisher_id(self):
        puburl = '/publish'
        service_type = 'foobar'

        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port" : "1111" },
            'service-type' : '%s' % service_type,
            'service-id' : 'test-discovery',
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        suburl = "/subscribe"
        payload = {
            'service'     : '%s' % service_type,
            'client'      : 'test-discovery',
            'instances'   : 1,
            'client-type' : 'test-discovery',
        }

        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 1)
        for entry in response[service_type]:
            self.assertEqual("@publisher-id" in entry, True)
