import sys
sys.path.append("../common/tests")
from test_utils import *
import test_common
sys.path.insert(0, '../../../../build/production/config/device-manager/')

from vnc_api.vnc_api import *
import uuid
from ncclient import manager
from flexmock import flexmock

class DMTestCase(test_common.TestCase):
    def setUp(self):
        super(DMTestCase, self).setUp()
        flexmock(manager, connect=fake_netconf_connect)
        self._dm_greenlet = gevent.spawn(test_common.launch_device_manager,
            self._api_server_ip, self._api_server_port)

    def tearDown(self):
        self._dm_greenlet.kill()
        super(DMTestCase, self).tearDown()

#end DMTestCase

