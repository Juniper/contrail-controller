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
    def check_dm_state(self):
        try:
            dm_cs = DMCassandraDB.getInstance() 
            if dm_cs:
                self.assertTrue(True) 
        except:
            self.assertTrue(False) 
        
    # test upgrade case of irb ip persistance, key is modified
    def test_db_upgrade(self):
        #wait for dm
        self.check_dm_state()
        dm_cs = DMCassandraDB.getInstance() 
        ip_addr = '10.0.0.1/32'
        key = 'pr-uuid' + ':' + 'vn-uuid' + ':' + 'sub-prefix'
        new_key = 'pr-uuid' + ':' + 'vn-uuid' + ':' + 'sub-prefix' + '_' + 'irb'
        dm_cs.add(dm_cs._PR_VN_IP_CF, key, {'ip_address': ip_addr})
        dm_cs.init_pr_map()

        #check: old key should not be present, new key should have value
        old_key_value = dm_cs.get(dm_cs._PR_VN_IP_CF, key)
        new_key_value = dm_cs.get(dm_cs._PR_VN_IP_CF, new_key)
        if old_key_value or not new_key_value or new_key_value.get('ip_address') != ip_addr:
            self.assertTrue(False)
        return 
    # end

# end TestInfraDM

