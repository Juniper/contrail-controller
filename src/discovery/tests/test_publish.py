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
    def test_publish_json(self):
        service_type = 'foobar'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" }
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

    def test_publish_json_new(self):
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

        """
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        self.assertEqual(response['services'][0]['service_type'], service_type)
        """

    def test_publish_xml(self):
        service_type = 'foobar2'
        payload = '\
          <%s>\
            <ip-addr>2.2.2.2</ip-addr>\
            <port>4321</port>\
          </%s>\
          ' % (service_type, service_type)
        puburl = '/publish/test_discovery'
        self._set_content_type('application/xml')
        (code, msg) = self._http_post(puburl, payload)
        self.assertEqual(code, 200)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        self.assertEqual(response['services'][0]['service_type'], service_type)

    def test_publish_xml_new(self):
        service_type = 'foobar2'
        payload = '\
          <publish>\
            <%s>\
              <ip-addr>2.2.2.2</ip-addr>\
              <port>4321</port>\
            </%s>\
            <service-type>%s</service-type>\
          <publish>\
          ' % (service_type, service_type, service_type)
        puburl = '/publish/test_discovery'
        self._set_content_type('application/xml')
        (code, msg) = self._http_post(puburl, payload)
        self.assertEqual(code, 500)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        """
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 0)
        self.assertEqual(response['services'][0]['service_type'], service_type)
        """
