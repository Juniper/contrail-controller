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
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)
        self._dm_greenlet = gevent.spawn(test_common.launch_device_manager,
            self._api_server_ip, self._api_server_port)

    def tearDown(self):
        self._dm_greenlet.kill()
        self._st_greenlet.kill()
        super(DMTestCase, self).tearDown()

    def _get_ip_fabric_ri_obj(self):
        # TODO pick fqname hardcode from common
        rt_inst_obj = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end _get_ip_fabric_ri_obj

    def create_router(self, name, mgmt_ip):
        bgp_router = BgpRouter(name, parent_obj=self._get_ip_fabric_ri_obj())
        params = BgpRouterParams()
        params.address = mgmt_ip
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 64512
        params.vendor = 'mx'
        params.vnc_managed = True
        bgp_router.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(bgp_router)

        pr = PhysicalRouter(name)
        pr.physical_router_management_ip = mgmt_ip
        pr.physical_router_vendor_name = 'mx'
        uc = UserCredentials('user', 'pw')
        pr.set_physical_router_user_credentials(uc)
        pr.set_bgp_router(bgp_router)
        pr_id = self._vnc_lib.physical_router_create(pr)

        return bgp_router, pr

#end DMTestCase

