import sys
sys.path.append('../common/tests')
import uuid
import json
from flexmock import flexmock, Mock
import stevedore.extension
from test_utils import *
import test_common
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


class FakeExtensionManager(object):
    _entry_pt_to_classes = {}
    class FakeExtObj(object):
        def __init__(self, cls, *args, **kwargs):
            self.obj = cls(*args, **kwargs)

    def __init__(self, child, ep_name, **kwargs):
        if ep_name not in self._entry_pt_to_classes:
            return

        classes = self._entry_pt_to_classes[ep_name]
        self._ep_name = ep_name
        self._ext_objs = []
        for cls in classes:
            ext_obj = FakeExtensionManager.FakeExtObj(cls, **kwargs) 
            self._ext_objs.append(ext_obj)
    # end __init__

    def map(self, cb):
        for ext_obj in self._ext_objs:
            cb(ext_obj)

    def map_method(self, method_name, *args, **kwargs):
        for ext_obj in self._ext_objs:
            method = getattr(ext_obj.obj, method_name, None)
            if not method:
                continue
            method(*args, **kwargs)
# end class FakeExtensionManager


class FakeKeystoneClient(object):
    class Tenants(object):
        _tenants = {}
        def add_tenant(self, id, name):
            self.id = id
            self.name = name
            self._tenants[id] = self

        def list(self):
            return self._tenants.values()

        def get(self, id):
            return self._tenants[str(uuid.UUID(id))]

    def __init__(self, *args, **kwargs):
        self.tenants = FakeKeystoneClient.Tenants()
        pass

# end class FakeKeystoneClient

fake_keystone_client = FakeKeystoneClient()
def get_keystone_client(*args, **kwargs):
    return fake_keystone_client

def vnc_setup_flexmock(mocks):
    for (cls, method_name, val) in mocks:
        kwargs = {method_name: val}
        flexmock(cls, **kwargs)

class VncOpenstackTestCase(test_common.TestCase):
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
        vnc_setup_flexmock([(stevedore.extension.ExtensionManager, '__new__', FakeExtensionManager)])
        super(VncOpenstackTestCase, self).setUp()
    # end setUp
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
        vnc_setup_flexmock([(keystone.Client, '__new__', get_keystone_client)])
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
