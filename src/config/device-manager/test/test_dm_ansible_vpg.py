#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from test_dm_utils import FakeJobHandler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_common import retry_exc_handler
from test_common import retries
import sys
sys.path.append("../common/tests")

class TestAnsibleVpgDM(TestAnsibleCommonDM):

    @classmethod
    def setUpClass(cls):
        dm_config_knobs = [
            ('DEFAULTS', 'push_mode', '1')
        ]
        super(TestAnsibleVpgDM, cls).setUpClass(
            dm_config_knobs=dm_config_knobs)
    # end setUpClass

    def test_create_prereq(self):
        #create a fabric
        fab_uuid = self.create_fabric('fabric_lag_mh')
        #create PR1
        _, pr1 = self.create_router('router1' + self.id(), '1.1.1.1',
                                   role='leaf', ignore_bgp=True)
        #assign fabric as PR ref
        pr1.set_fabric_list([
            {
                "uuid": fab_uuid
            }
        ])
        pr1.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr1)

        #create PR2
        _, pr2 = self.create_router('router2' + self.id(), '2.2.2.2',
                                   role='leaf', ignore_bgp=True)
        #ssign fabric as PR ref
        pr2.set_fabric_list([
            {
                "uuid": fab_uuid
            }
        ])
        pr2.set_physical_router_loopback_ip('20.20.0.1')
        self._vnc_lib.physical_router_update(pr2)

        #create VN
        
        self.delete_routers(pr=pr1)
        self.delete_routers(pr=pr2)
        self.delete_fabric(id=fab_uuid)
# end TestAnsibleVpgDM
