import sys
sys.path.append('../common/tests')
import uuid
import json
from flexmock import flexmock, Mock
from testtools.matchers import Equals, Contains, Not
import stevedore.extension
from test_utils import *
from test_common import TestCase, setup_extra_flexmock
import ConfigParser

from vnc_cfg_api_server import vnc_cfg_api_server

import bottle
import vnc_openstack

@bottle.hook('after_request')
def after_request():
    bottle.response.headers['Content-Type'] = 'application/json; charset="UTF-8"'
    try:
        del bottle.response.headers['Content-Length']
    except KeyError:
        pass


class VncOpenstackTestCase(TestCase):
    def __init__(self, *args, **kwargs):
        super(VncOpenstackTestCase, self).__init__(*args, **kwargs)
        self._config_knobs = [
            ('DEFAULTS', '', ''),
            ('KEYSTONE', 'admin_user', ''),
            ('KEYSTONE', 'admin_password', ''),
            ('KEYSTONE', 'admin_tenant_name', ''),
            ('KEYSTONE', 'admin_token', ''),
            ('KEYSTONE', 'auth_host', ''),
            ('KEYSTONE', 'auth_port', ''),
            ('KEYSTONE', 'auth_protocol', 'http'),
            ]
    # end __init__

    def setUp(self):
        setup_extra_flexmock([(stevedore.extension.ExtensionManager, '__new__', FakeExtensionManager)])
        super(VncOpenstackTestCase, self).setUp()
    # end setUp

    def tearDown(self):
        with open('vnc_openstack.err') as f:
            self.assertThat(len(f.read()), Equals(0),
                            "Error log in vnc_openstack.err")
        super(VncOpenstackTestCase, self).tearDown()
# end class VncOpenstackTestCase

class NeutronBackendTestCase(VncOpenstackTestCase):
    def setUp(self):
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.neutronApi'] = [vnc_openstack.NeutronApiDriver]
        super(NeutronBackendTestCase, self).setUp()
    # end setUp

    def tearDown(self):
        del FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.neutronApi']
        super(NeutronBackendTestCase, self).tearDown()
    # end tearDown

# end class NeutronBackendTestCase

class KeystoneSyncTestCase(VncOpenstackTestCase):
    def setup_flexmock(self):
        import keystoneclient.v2_0.client as keystone
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resync'] = [vnc_openstack.OpenstackDriver]
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = [vnc_openstack.ResourceApiDriver]
        setup_extra_flexmock([(keystone.Client, '__new__', get_keystone_client)])
    # end setup_flexmock

    def setUp(self):
        self.setup_flexmock()
        super(KeystoneSyncTestCase, self).setUp()
    # end setUp

    def tearDown(self):
        del FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resync']
        del FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi']
        super(KeystoneSyncTestCase, self).tearDown()
    # end tearDown
# end class KeystoneSyncTestCase
