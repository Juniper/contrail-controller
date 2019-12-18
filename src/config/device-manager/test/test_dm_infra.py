#
# Copyright (c) 2013 Juniper Infra, Inc. All rights reserved.
#
from __future__ import absolute_import
import sys
import gevent
from unittest import skip
from vnc_api.vnc_api import *
from cfgm_common.vnc_db import DBBase
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from gevent import monkey
monkey.patch_all()
sys.path.append('../common/cfgm_common/tests/mocked_libs')
from device_manager.db import DMCassandraDB
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from cfgm_common.tests.test_common import launch_device_manager
from cfgm_common.tests.test_common import kill_device_manager
from cfgm_common.tests.test_common import wait_for_device_manager_up
from .test_dm_common import *
from .test_dm_utils import FakeDeviceConnect
from .test_dm_utils import FakeJobHandler
from .test_dm_utils import FakeNetconfManager

#
# All Infra related DM test cases should go here
#
class TestInfraDM(TestCommonDM):

    def __init__(self, *args, **kwargs):
        self.product = "mx"
        super(TestInfraDM, self).__init__(*args, **kwargs)

    @retries(5, hook=retry_exc_handler)
    def check_if_xml_is_generated(self):
        pr_config = FakeDeviceConnect.params.get("pr_config")
        new_config = FakeDeviceConnect.params.get("config")
        operation = FakeDeviceConnect.params.get("operation")
        conf = pr_config.build_conf(new_config, operation)
        xml_conf = pr_config.serialize(conf)
        if not xml_conf or 'config' not in xml_conf or 'apply-groups' not in xml_conf:
            self.assertTrue(False)
        return

    # check for xml conf generation, very basic validation
    def test_dm_xml_generation(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                                          product=self.product)
        self.check_if_xml_is_generated()
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    # check for greenlets, bug: #1714004
    def test_dm_greenlets(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                    product=self.product)
        self.check_if_xml_is_generated()
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        pr.del_bgp_router(bgp_router)
        # delete bgp link, update and immediately delete PR
        self._vnc_lib.physical_router_update(pr)
        self._vnc_lib.physical_router_delete(fq_name=pr_fq)
        # create again PR with same FQ/ip and link it with BGP
        _, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                   product=self.product, ignore_bgp=True)
        pr.set_bgp_router(bgp_router)
        gevent.sleep(5)
        self._vnc_lib.physical_router_update(pr)

        # netconf push should happen as expected
        self.check_if_xml_is_generated()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)
    # end test_dm_greenlets

    # no crash if bgp router paramters are not configured
    def test_dm_no_bgp_params(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1',
                                                          product=self.product)
        self.check_if_xml_is_generated()
        bgp_router_params = bgp_router.get_bgp_router_parameters()
        FakeDeviceConnect.params["config"] = None
        bgp_router.set_bgp_router_parameters(None)
        self._vnc_lib.bgp_router_update(bgp_router)
        self.check_if_config_is_not_pushed()
        bgp_router.set_bgp_router_parameters(bgp_router_params)
        self._vnc_lib.bgp_router_update(bgp_router)
        self.check_if_xml_is_generated()
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)
    # end test_dm_no_bgp_params

    @retries(5, hook=retry_exc_handler)
    def check_if_config_is_not_pushed(self):
        self.assertIsNone(FakeDeviceConnect.params.get("config"))

    # check for unsupoorted device xml generation
    def test_dm_unsupported_device(self):
        # product name, vendor is still configured as mx, juniper
        # but actual device is not mx
        bgp_router, pr = self.create_router('router1' + self.id(), '199.199.199.199',
                                                           product=self.product)
        self.check_if_config_is_not_pushed()
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    @retries(5, hook=retry_exc_handler)
    def check_dm_state(self):
        self.assertIsNotNone(DMCassandraDB.get_instance())

    # test dm private cassandra data
    def test_dm_cassandra(self):
        #wait for dm
        self.check_dm_state()
        dm_cs = DMCassandraDB.get_instance()
        ip_addr = '10.0.0.1/32'
        lo0_ip_addr = '20.0.0.1/32'
        key = 'pr-uuid' + ':' + 'vn-uuid' + ':' + 'sub-prefix'
        dm_cs.add(dm_cs._PR_VN_IP_CF, key, {DMUtils.get_ip_cs_column_name('irb'): ip_addr})
        dm_cs.add(dm_cs._PR_VN_IP_CF, key, {DMUtils.get_ip_cs_column_name('lo0'): lo0_ip_addr})
        dm_cs.init_pr_map()

        key_value = dm_cs._cassandra_driver.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('irb'))
        if key_value != ip_addr:
            self.assertTrue(False)
        key_value = dm_cs._cassandra_driver.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('lo0'))
        if key_value != lo0_ip_addr:
            self.assertTrue(False)

        # delete one column
        dm_cs.delete(dm_cs._PR_VN_IP_CF, key, [DMUtils.get_ip_cs_column_name('lo0')])

        try:
            key_value = dm_cs._cassandra_driver.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('lo0'))
            if key_value is not None:
                self.assertTrue(False)
        except NoIdError:
            pass

        key_value = dm_cs._cassandra_driver.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('irb'))
        if key_value != ip_addr:
            self.assertTrue(False)

        return
    # end

    @retries(5, hook=retry_exc_handler)
    def check_dm_instance(self):
        self.assertIsNone(DMCassandraDB.dm_object_db_instance)
        self.assertIsNone(DeviceManager.get_instance())
        self.assertIsNone(DBBase._object_db)

    # test dm instance
    def test_dm_instance(self):
        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        kill_device_manager(TestInfraDM._dm_greenlet)
        self.check_dm_instance()
        TestInfraDM._dm_greenlet = gevent.spawn(launch_device_manager,
            self._cluster_id, TestInfraDM._api_server_ip, TestInfraDM._api_server_port)
        wait_for_device_manager_up()

    # check dm plugin
    @retries(5, hook=retry_exc_handler)
    def check_dm_plugin(self, is_valid = True):
        if not is_valid:
            self.assertIsNone(FakeDeviceConnect.params.get("pr_config"))
            self.assertIsNone(FakeDeviceConnect.params.get("config"))
        else:
            pr = FakeDeviceConnect.params.get("pr_config")
            self.assertIsNotNone(pr)
            self.assertIsNotNone(FakeDeviceConnect.params.get("config"))

    @retries(5, hook=retry_exc_handler)
    def check_dm_delete_groups(self):
        if not FakeDeviceConnect.params:
            return
        pr_config = FakeDeviceConnect.params.get("pr_config")
        self.assertIsNotNone(pr_config)
        self.assertIsNotNone(FakeDeviceConnect.params.get("config"))
        xml_conf = FakeDeviceConnect.params.get("config")
        operation = FakeDeviceConnect.params.get("operation")
        self.assertEqual(operation, "delete")

    # check plugin registration
    def test_dm_plugins(self):
        # check basic valid vendor, product plugin
        bgp_router, pr = self.create_router('router100' + self.id(), '1.1.1.1',
                                                            product=self.product)
        self.check_dm_plugin()
        pr_config = FakeDeviceConnect.params.get("pr_config")

        # update valid another vendor, product; another plugin should be found
        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('qfx5110')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "qfx5110"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('qfx5100')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "qfx5100"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('qfx5200')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "qfx5200"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('qfx5300')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "qfx5300"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin(is_valid=False)

        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('qfx10000')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "qfx10000"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        # check invalid vendor, product; no plugin
        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('cix')
        pr.physical_router_vendor_name = "cix"
        pr.physical_router_product_name = "cix100"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin(is_valid=False)

        # update valid vendor, product; plugin should be found, config should be pushed
        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('mx80')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "mx"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('mx480')
        pr.physical_router_vendor_name = "juniper"
        pr.physical_router_product_name = "mx480"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        # product/vendor names can be case in-sensitive
        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        FakeNetconfManager.set_model('MX480')
        pr.physical_router_vendor_name = "JunIper"
        pr.physical_router_product_name = "mX480"
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_plugin()

        # unset vnc-managed, should generate delete groups config
        pr.physical_router_vnc_managed = False
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_delete_groups()

        # set vnc-managed, should generate groups config again
        pr.physical_router_vnc_managed = True
        self._vnc_lib.physical_router_update(pr)
        self.check_if_xml_is_generated()

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

# end TestInfraDM

