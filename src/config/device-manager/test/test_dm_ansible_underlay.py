#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
sys.path.append("../common/tests")

from test_common import retries
from test_common import retry_exc_handler
from test_dm_common import TestCommonDM
from test_dm_utils import FakeJobHandler


class TestAnsibleUnderlayDM(TestCommonDM):
    @classmethod
    def setUpClass(cls):
        dm_config_knobs = [
            ('DEFAULTS', 'push_mode', '1')
        ]
        super(TestAnsibleUnderlayDM, cls).setUpClass(
            dm_config_knobs=dm_config_knobs)
    # end setUpClass

    @retries(5, hook=retry_exc_handler)
    def check_dm_ansible_underlay_config_push(self):
        job_template = FakeJobHandler.get_job_template()
        job_input = FakeJobHandler.get_job_input()
        self.assertIsNotNone(job_input)
        self._logger.debug("Job Template: %s", job_template)
        self._logger.debug("Job Input: %s", job_input)
    # end check_dm_ansible_underlay_config_push

    def test_dm_ansible_underlay_config_push(self):
        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   role='leaf', ignore_bgp=True)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_ansible_underlay_config_push()
        pr_fq = pr.get_fq_name()
        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr_fq)
    # end verify_dm_ansible_underlay_config_push
# end TestAnsibleUnderlayDM
