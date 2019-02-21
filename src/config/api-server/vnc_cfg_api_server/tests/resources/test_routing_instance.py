#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import RoutingInstance
from vnc_api.vnc_api import VirtualNetwork

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestRoutingInstance(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestRoutingInstance, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRoutingInstance, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_cannot_create_default_routing_instance(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id())
        self.api.virtual_network_create(vn)

        ri1 = RoutingInstance('ri1-%s' % self.id(), parent_obj=vn)
        ri1.set_routing_instance_is_default(False)
        self.api.routing_instance_create(ri1)

        ri2 = RoutingInstance('ri2-%s' % self.id(), parent_obj=vn)
        ri2.set_routing_instance_is_default(True)
        self.assertRaises(BadRequest, self.api.routing_instance_create, ri2)

    def test_cannot_update_routing_instance_to_default(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id())
        self.api.virtual_network_create(vn)
        ri = RoutingInstance('ri-%s' % self.id(), parent_obj=vn)
        self.api.routing_instance_create(ri)

        ri.set_routing_instance_is_default(True)
        self.assertRaises(BadRequest, self.api.routing_instance_update, ri)

    def test_cannot_update_default_routing_instance_to_not_default(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id())
        self.api.virtual_network_create(vn)
        default_ri_fq_name = vn.fq_name + [vn.fq_name[-1]]
        default_ri = self.api.routing_instance_read(default_ri_fq_name)

        default_ri.set_routing_instance_is_default(False)
        self.assertRaises(BadRequest, self.api.routing_instance_update,
                          default_ri)

    def test_cannot_delete_default_routing_instance(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id())
        self.api.virtual_network_create(vn)
        default_ri_fq_name = vn.fq_name + [vn.fq_name[-1]]
        default_ri = self.api.routing_instance_read(default_ri_fq_name)

        self.assertRaises(BadRequest, self.api.routing_instance_delete,
                          id=default_ri.uuid)

        self.api.virtual_network_delete(id=vn.uuid)
