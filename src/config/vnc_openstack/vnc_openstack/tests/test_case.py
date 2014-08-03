import sys
sys.path.append('../common/tests')
import json
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

class NeutronBackendTestCase(test_common.TestCase):
    def __init__(self, *args, **kwargs):
        self._config_knobs = [
            ('DEFAULTS', '', ''),
            ('KEYSTONE', 'admin_user', ''),
            ('KEYSTONE', 'admin_password', ''),
            ('KEYSTONE', 'admin_tenant_name', ''),
            ]
        super(NeutronBackendTestCase, self).__init__(*args, **kwargs)
    # end __init__

    def setUp(self):
        def fake_load_extensions(api_server_obj):
            self._api_server_ip = api_server_obj._args.listen_ip_addr
            self._api_server_port = api_server_obj._args.listen_port
         
            conf_sections = ConfigParser.SafeConfigParser()
            for (section, var, val) in self._config_knobs:
                try:
                    conf_sections.add_section(section)
                except ConfigParser.DuplicateSectionError:
                    pass
                conf_sections.set(section, var, str(val))
     
            vnc_openstack.NeutronApiDriver(self._api_server_ip,
                self._api_server_port, conf_sections)
        # end fake_load_extensions

        vnc_cfg_api_server.VncApiServer._load_extensions = fake_load_extensions
        super(NeutronBackendTestCase, self).setUp()
    # end setUp

    def tearDown(self):
        super(NeutronBackendTestCase, self).tearDown()
# end NeutronBackendTestCase
