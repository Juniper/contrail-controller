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

        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        self.assertEqual(response['services'][0]['service_type'], service_type)

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
          </publish>\
          ' % (service_type, service_type, service_type)
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

    def test_publish_ip_addr(self):
        service_type = 'foobar'
        my_ip_addr = 'dummy-string'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
            'remote-addr'  : '%s' % my_ip_addr,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        time.sleep(1)
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)

        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)

        entry = response['services'][0]
        self.assertEqual(entry['remote'], my_ip_addr)

    def test_oper_state(self):
        service_type = 'foobar'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        # service operational state is up by default
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        entry = response['services'][0]
        self.assertEqual(entry['oper_state'], 'up')

        # set operational state down - should not impact admin state
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
            'oper-state'   : 'down',
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        entry = response['services'][0]
        self.assertEqual(entry['oper_state'], 'down')
        self.assertEqual(entry['admin_state'], 'up')

        # republish without oper-state - should still be down
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        entry = response['services'][0]
        self.assertEqual(entry['oper_state'], 'down')
        self.assertEqual(entry['admin_state'], 'up')

        service_count = 1
        tasks = []
        disc = client.DiscoveryClient(self._disc_server_ip,
                   self._disc_server_port, "test-publish")
        obj = disc.subscribe(service_type, service_count, info_callback)
        tasks.append(obj.task)
        print 'Started task to subscribe service %s, count %d' \
            % (service_type, service_count)

        # validate service in oper-state down is not assigned
        time.sleep(1)
        (code, msg) = self._http_get('/clients.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 0)

        # set oper-state up - subscription should happen
        payload['oper-state'] = 'up'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        entry = response['services'][0]
        self.assertEqual(entry['oper_state'], 'up')
        self.assertEqual(entry['admin_state'], 'up')

        # validate service in up state should get assigned
        service_count = 1
        tasks = []
        disc = client.DiscoveryClient(self._disc_server_ip,
                   self._disc_server_port, "test-publish")
        obj = disc.subscribe(service_type, service_count, info_callback)
        tasks.append(obj.task)
        print 'Started task to subscribe service %s, count %d' \
            % (service_type, service_count)

        # validate service in oper-state down is not assigned
        time.sleep(1)
        (code, msg) = self._http_get('/clients.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)

    def test_service_update_api(self):
        service_type = 'foobar'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)

        # service is up by default
        entry = response['services'][0]
        self.assertEqual(entry['admin_state'], 'up')
        self.assertEqual(entry['oper_state'], 'up')

        puburl = '/service/test_discovery'
        payload = {
            'service-type'  : service_type,
            'admin-state'   : 'down',
            'oper-state'    : 'down',
        }
        (code, msg) = self._http_put(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        entry = response['services'][0]
        self.assertEqual(entry['admin_state'], 'down')
        self.assertEqual(entry['oper_state'], 'down')

    def test_publish_admin_state(self):
        service_type = 'foobar'
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        puburl = '/publish/test_discovery'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)

        # service is up by default
        entry = response['services'][0]
        self.assertEqual(entry['admin_state'], 'up')
        self.assertEqual(entry['oper_state'], 'up')

        payload['admin-state'] = 'down'
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)
        self.assertEqual(entry['oper_state'], 'up')

        entry = response['services'][0]
        self.assertEqual(entry['admin_state'], 'down')

        # republish without admin-state - should still be down
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port"    : "1234" },
            'service-type' : '%s' % service_type,
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 1)

        entry = response['services'][0]
        self.assertEqual(entry['admin_state'], 'down')
        self.assertEqual(entry['oper_state'], 'up')
