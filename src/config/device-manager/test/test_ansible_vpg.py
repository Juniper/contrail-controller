#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
sys.path.append("../common/tests")

import gevent
import json
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from test_common import retries
from test_common import retry_exc_handler
from test_dm_ansible import TestAnsibleDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestAnsibleVpgDM(TestAnsibleDM):

    def test_lag_config_push(self):
        #create objects
        fabric = self.create_fabric('test-fabric-vpg')

        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx5100', family='junos-qfx',
                                   role='leaf', rb_roles=['crb-access'],
                                   fabric=fabric)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)


        vn_obj = self.create_vn('1', '1.1.1.0')

        vm_uuid = self.create_vm("bms1")

        self.create_vmi(pr_pi, fabric, True, vm_uuid, vn_obj)





