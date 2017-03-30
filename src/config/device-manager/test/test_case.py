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
from test_dm_utils import FakeDeviceConnect
from test_dm_utils import fake_netconf_connect
from test_dm_utils import fake_send_netconf

class DMTestCase(test_common.TestCase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config.append(extra_config_knobs)
        super(DMTestCase, cls).setUpClass(extra_config_knobs=extra_config)
        cls._dm_greenlet = gevent.spawn(test_common.launch_device_manager,
            cls.__name__, cls._api_server_ip, cls._api_server_port)
        test_common.wait_for_device_manager_up()
        cls._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            cls.__name__, cls._api_server_ip, cls._api_server_port)
        test_common.wait_for_schema_transformer_up()

    @classmethod
    def tearDownClass(cls):
        test_common.kill_device_manager(cls._dm_greenlet)
        test_common.kill_schema_transformer(cls._st_greenlet)
        super(DMTestCase, cls).tearDownClass()

    def setUp(self, extra_config_knobs=None):
        super(DMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)
        flexmock(manager, connect=fake_netconf_connect)
        setattr(PhysicalRouterConfig, 'send_netconf', fake_send_netconf)

    def tearDown(self):
        FakeDeviceConnect.reset()
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

    @test_common.retries(5, hook=test_common.retry_exc_handler)
    def wait_for_routers_delete(self, bgp_fq=None, pr_fq=None):
        found = False
        if bgp_fq:
            try:
                self._vnc_lib.bgp_router_read(fq_name=bgp_fq)
                found = True
            except NoIdError:
                pass
        if pr_fq:
            try:
                self._vnc_lib.physical_router_read(fq_name=pr_fq)
                found = True
            except NoIdError:
                pass
        self.assertFalse(found)
        return

#end DMTestCase

