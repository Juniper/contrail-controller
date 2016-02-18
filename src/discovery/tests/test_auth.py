
import sys
import time
sys.path.append("../config/common/tests")
from test_utils import *
import fixtures
import testtools
import test_utils
import test_common
import test_case

import discoveryclient.client as client

from vnc_api.vnc_api import *
import keystoneclient.v2_0.client as keystone
try:
    from keystoneclient.middleware import auth_token
except ImportError:
    from keystonemiddleware import auth_token
except Exception:
    pass

subscribe_info = ''
def info_callback(info):
    global subscribe_info
    subscribe_info = info
    # print 'In subscribe callback handler'
    # print '%s' % (info)
    pass

def token_from_user_info(user_name, tenant_name, domain_name, role_name,
        tenant_id = None):
    token_dict = {
        'X-User': user_name,
        'X-User-Name': user_name,
        'X-Project-Name': tenant_name,
        'X-Project-Id': tenant_id or '',
        'X-Domain-Name' : domain_name,
        'X-Role': role_name,
    }
    rval = json.dumps(token_dict)
    # logger.info( '**** Generate token %s ****' % rval)
    return rval

# This is needed for VncApi._authenticate invocation from within Api server.
# We don't have access to user information so we hard code admin credentials.
def ks_admin_authenticate(self, response=None, headers=None):
    rval = token_from_user_info('admin', 'admin', 'default-domain', 'admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers

class DiscoveryServerTestCase(test_case.DsTestCase):
    def setUp(self):
        extra_config_knobs = [
            ('DEFAULTS', 'auth', 'keystone'),
        ]
        extra_mocks = [(auth_token, 'AuthProtocol',
                            test_utils.FakeAuthProtocol)]
        super(DiscoveryServerTestCase, self).setUp(extra_disc_server_mocks=extra_mocks,
            extra_disc_server_config_knobs=extra_config_knobs)

    def test_service_admin_state(self):
        service_type = 'foobar'
        puburl = '/publish/test_discovery'
        updurl = '/service/test_discovery'
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

        # service is up by default
        entry = response['services'][0]
        self.assertEqual(entry['admin_state'], 'up')

        payload = {
            'service-type'  : service_type,
            'admin-state'   : 'down',
        }
        (code, msg) = self._http_put(updurl, json.dumps(payload))
        self.assertEqual(code, 401)

        # try as non-admin - should fail
        headers = {}
        headers['X-AUTH-TOKEN'] = token_from_user_info('alice', 'admin', 'default-domain', 'alice-role')
        (code, msg) = self._http_put(updurl, json.dumps(payload), http_headers=headers)
        self.assertEqual(code, 401)

        # try as admin - should pass
        headers = {}
        headers['X-AUTH-TOKEN'] = token_from_user_info('admin', 'admin', 'default-domain', 'admin')
        (code, msg) = self._http_put(updurl, json.dumps(payload), http_headers=headers)
        self.assertEqual(code, 200)
