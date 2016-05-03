import sys
import uuid
import json
import ConfigParser

import bottle
from flexmock import flexmock, Mock
from testtools.matchers import Equals, Contains, Not
import stevedore.extension

from vnc_cfg_api_server import vnc_cfg_api_server
sys.path.append('../common/tests')
from test_utils import *
from test_common import TestCase, setup_extra_flexmock

import fake_neutron
import vnc_openstack

@bottle.hook('after_request')
def after_request():
    bottle.response.headers['Content-Type'] = 'application/json; charset="UTF-8"'
    try:
        del bottle.response.headers['Content-Length']
    except KeyError:
        pass


class VncOpenstackTestCase(TestCase):
    _config_knobs = [
            ('DEFAULTS', '', ''),
            ('KEYSTONE', 'admin_user', ''),
            ('KEYSTONE', 'admin_password', ''),
            ('KEYSTONE', 'admin_tenant_name', ''),
            ('KEYSTONE', 'admin_token', ''),
            ('KEYSTONE', 'auth_host', ''),
            ('KEYSTONE', 'auth_port', ''),
            ('KEYSTONE', 'auth_protocol', 'http'),
    ]
    _entry_pt_to_classes = FakeExtensionManager._entry_pt_to_classes
    @classmethod
    def setup_flexmock(cls):
        import keystoneclient.v2_0.client as keystone
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resync'] = [vnc_openstack.OpenstackDriver]
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = [vnc_openstack.ResourceApiDriver]
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.neutronApi'] = [vnc_openstack.NeutronApiDriver]
        setup_extra_flexmock([(keystone.Client, '__new__', get_keystone_client)])
    # end setup_flexmock

    def __init__(self, *args, **kwargs):
        super(VncOpenstackTestCase, self).__init__(*args, **kwargs)
    # end __init__

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        setup_extra_flexmock([(stevedore.extension.ExtensionManager, '__new__', FakeExtensionManager)])
        super(VncOpenstackTestCase, cls).setUpClass(*args, **kwargs)
    # end setUp

    @classmethod
    def tearDownClass(cls):
        try:
            with open('vnc_openstack.err') as f:
                cls.assertThat(len(f.read()), Equals(0),
                                "Error log in vnc_openstack.err")
        except IOError:
            # vnc_openstack.err not created, No errors.
            pass
        FakeExtensionManager._entry_pt_to_classes = cls._entry_pt_to_classes
        super(VncOpenstackTestCase, cls).tearDownClass()
# end class VncOpenstackTestCase

class NeutronBackendTestCase(VncOpenstackTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.setup_flexmock()
        super(NeutronBackendTestCase, cls).setUpClass(*args, **kwargs)
    # end setUpClass
# end class NeutronBackendTestCase

class KeystoneSyncTestCase(VncOpenstackTestCase):
    @classmethod
    def setUpClass(cls):
        cls.setup_flexmock()
        super(KeystoneSyncTestCase, cls).setUpClass()
    # end setUpClass
# end class KeystoneSyncTestCase

class ResourceDriverTestCase(VncOpenstackTestCase):
    @classmethod
    def setUpClass(cls):
        cls.setup_flexmock()
        super(ResourceDriverTestCase, cls).setUpClass()
    # end setUp
# end class ResourceDriverTestCase
