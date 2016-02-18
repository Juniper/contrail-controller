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
    def setUp(self):
        extra_config_knobs = [
            ('DEFAULTS', 'white_list_publish', '127.0.0.1'),
            ('DEFAULTS', 'white_list_subscribe', '127.0.0.1'),
        ]
        super(DiscoveryServerTestCase, self).setUp(extra_disc_server_config_knobs=extra_config_knobs)

    # simple publish - for sanity
    def test_publish_basic(self):
        service_type = 'foobar'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        self.assertEqual(response['services'][0]['service_type'], service_type)

    # Attempt to publish from non white-listed address should fail with 401 error
    def test_publish_whitelist(self):
        service_type = 'foobar'
        headers = {'X-Forwarded-For': "1.1.1.1"}
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload), http_headers=headers)
        self.assertEqual(code, 401)

    def test_subscribe_basic(self):
        service_type = 'foobar'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        # json subscribe request
        suburl = "/subscribe"
        payload = {
            'service'     : '%s' % service_type,
            'instances'   : 1,
            'client-type' : 'test-discovery',
            'client'      : 'No-X-Forwarded-For-Header',
        }

        # should subscribe successfully
        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 1)

        # Attempt to subscribe from non white-listed address should fail with 401 error
        headers = {'X-Forwarded-For': "1.1.1.1"}
        payload['client'] = 'X-Forwarded-For-Header'
        (code, msg) = self._http_post(suburl, json.dumps(payload), http_headers=headers)
        self.assertEqual(code, 401)
