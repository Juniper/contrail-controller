#
# Copyright (c) 2013 Juniper Infra, Inc. All rights reserved.
#
import sys
import gevent
from time import sleep
sys.path.append("../common/tests")
from test_utils import *
from vnc_api.vnc_api import *
from cfgm_common.vnc_db import DBBase
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from gevent import monkey
monkey.patch_all()
from device_manager.db import DMCassandraDB
from device_manager.db import DBBaseDM
from device_manager.device_manager import DeviceManager
from test_common import *
from test_dm_common import *
from test_case import DMTestCase
from test_dm_utils import FakeDeviceConnect

#
# All Infra related DM test cases should go here
#
class TestInfraDM(TestCommonDM):

    @retries(5, hook=retry_exc_handler)
    def check_if_xml_is_generated(self):
        pr_config = FakeDeviceConnect.params.get("pr_config")
        new_config = FakeDeviceConnect.params.get("config")
        operation = FakeDeviceConnect.params.get("operation")
        conf = pr_config.build_netconf_config(new_config, operation)
        xml_conf = pr_config.get_xml_data(conf)
        if not xml_conf or 'config' not in xml_conf or 'apply-groups' not in xml_conf:
            self.assertTrue(False)
        return

    # check for xml conf generation, very basic validation
    def test_dm_xml_generation(self):
        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1')
        self.check_if_xml_is_generated()
        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    @retries(5, hook=retry_exc_handler)
    def check_if_config_is_not_pushed(self):
        self.assertIsNone(FakeDeviceConnect.params.get("config"))

    # check for unsupoorted device xml generation
    def test_dm_unsupported_device(self):
        # product name, vendor is still configured as mx, juniper
        # but actual device is not mx
        bgp_router, pr = self.create_router('router1' + self.id(), '199.199.199.199')
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

        key_value = dm_cs.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('irb'))
        if key_value != ip_addr:
            self.assertTrue(False)
        key_value = dm_cs.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('lo0'))
        if key_value != lo0_ip_addr:
            self.assertTrue(False)

        # delete one column
        dm_cs.delete(dm_cs._PR_VN_IP_CF, key, [DMUtils.get_ip_cs_column_name('lo0')])

        try:
            key_value = dm_cs.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('lo0'))
            if key_value is not None:
                self.assertTrue(False)
        except NoIdError:
            pass

        key_value = dm_cs.get_one_col(dm_cs._PR_VN_IP_CF, key, DMUtils.get_ip_cs_column_name('irb'))
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
        kill_device_manager(TestInfraDM._dm_greenlet)
        self.check_dm_instance()
        TestInfraDM._dm_greenlet = gevent.spawn(launch_device_manager,
            "DM-Test-Suite", TestInfraDM._api_server_ip, TestInfraDM._api_server_port)
        wait_for_device_manager_up()

# end TestInfraDM

