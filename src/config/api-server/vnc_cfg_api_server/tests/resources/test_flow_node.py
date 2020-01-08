#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging

from vnc_api.vnc_api import FlowNode
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import Project

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestFlowNode(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFlowNode, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFlowNode, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestFlowNode, self).setUp()
        self.gsc = self.api.global_system_config_read(
            GlobalSystemConfig().fq_name)

    @property
    def api(self):
        return self._vnc_lib

    def test_flow_node_crud(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        fn = FlowNode(name='fn-%s' % self.id(),
                      parent_obj=self.gsc)
        fn.set_flow_node_load_balancer_ip('10.100.0.55')
        fn.set_flow_node_ip_address('10.100.1.10')
        fn.set_flow_node_inband_interface('fn-test-inband')

        uuid = self.api.flow_node_create(fn)
        fn.set_uuid(uuid)
        fn.set_display_name('fn test')

        self.api.flow_node_update(fn)
        updated_fn = self.api.flow_node_read(id=fn.get_uuid())

        for attr in ['flow_node_load_balancer_ip',
                     'flow_node_ip_address',
                     'flow_node_inband_interface']:
            self.assertEqual(getattr(fn, attr), getattr(updated_fn, attr))
