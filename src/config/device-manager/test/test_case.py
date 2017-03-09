import sys
sys.path.append("../common/tests")
from test_utils import *
import test_common
sys.path.insert(0, '../../../../build/production/config/device-manager/')
sys.path.insert(0, '../../../../build/debug/config/device-manager/device_manager')
sys.path.insert(0, '../../../../build/debug/config/device-manager/device_api')

from vnc_api.vnc_api import *
import uuid
from random import randint
from ncclient import manager
from flexmock import flexmock
from device_manager.physical_router_config import PhysicalRouterConfig

class DMTestCase(test_common.TestCase):
    def setUp(self):
        super(DMTestCase, self).setUp()
        flexmock(manager, connect=fake_netconf_connect)
        setattr(PhysicalRouterConfig, 'send_netconf', fake_send_netconf)
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port)
        self._dm_greenlet = gevent.spawn(test_common.launch_device_manager,
            self.id(), self._api_server_ip, self._api_server_port)

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

    def create_router(self, name, mgmt_ip, ignore_pr=False):
        bgp_router = BgpRouter(name, parent_obj=self._get_ip_fabric_ri_obj())
        params = BgpRouterParams()
        params.address = mgmt_ip
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = randint(0, 64512)
        bgp_router.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(bgp_router)

        if ignore_pr:
            return bgp_router, None
        pr = PhysicalRouter(name)
        pr.physical_router_management_ip = mgmt_ip
        pr.physical_router_vendor_name = 'juniper'
        pr.physical_router_product_name = 'mx'
        pr.physical_router_vnc_managed = True
        uc = UserCredentials('user', 'pw')
        pr.set_physical_router_user_credentials(uc)
        pr.set_bgp_router(bgp_router)
        pr_id = self._vnc_lib.physical_router_create(pr)

        return bgp_router, pr

    def delete_routers(self, bgp_router=None, pr=None):
        if pr:
            self._vnc_lib.physical_router_delete(fq_name=pr.get_fq_name())
        if bgp_router:
            self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())
        return

#end DMTestCase

