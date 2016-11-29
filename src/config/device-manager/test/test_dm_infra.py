#
# Copyright (c) 2013 Juniper Infra, Inc. All rights reserved.
#
import sys
import gevent
from time import sleep
sys.path.append("../common/tests")
from test_utils import *
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from gevent import monkey
monkey.patch_all()
from device_manager.db import DMCassandraDB
from test_common import *
from test_dm_common import *

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
        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        gevent.sleep(2)
        self.check_if_xml_is_generated()

    @retries(5, hook=retry_exc_handler)
    def check_dm_state(self):
        try:
            dm_cs = DMCassandraDB.getInstance()
            if dm_cs:
                self.assertTrue(True)
        except:
            self.assertTrue(False)

    # test dm private cassandra data
    def test_dm_cassandra(self):
        #wait for dm
        self.check_dm_state()
        dm_cs = DMCassandraDB.getInstance()
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

# end TestInfraDM

