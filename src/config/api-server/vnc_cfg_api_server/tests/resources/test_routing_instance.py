#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common import CANNOT_MODIFY_MSG
from cfgm_common.exceptions import RefsExistError
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import RoutingInstance
from vnc_api.vnc_api import ServiceChainInfo
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
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        ri1 = RoutingInstance('ri1-%s' % self.id(), parent_obj=vn)
        ri1.set_routing_instance_is_default(False)
        self.api.routing_instance_create(ri1)

        ri2 = RoutingInstance('ri2-%s' % self.id(), parent_obj=vn)
        ri2.set_routing_instance_is_default(True)
        msg = CANNOT_MODIFY_MSG % {
            'resource_type':
                RoutingInstance.object_type.replace('_', ' ').title(),
            'fq_name': ri2.get_fq_name_str(),
            'uuid': '.*',
        }
        msg = r'^%s$' % msg.replace('(', r'\(').replace(')', r'\)')
        with self.assertRaisesRegex(RefsExistError, msg):
            self.api.routing_instance_create(ri2)

    def test_cannot_update_routing_instance_to_default(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        ri = RoutingInstance('ri-%s' % self.id(), parent_obj=vn)
        self.api.routing_instance_create(ri)

        ri.set_routing_instance_is_default(True)
        msg = CANNOT_MODIFY_MSG % {
            'resource_type':
                RoutingInstance.object_type.replace('_', ' ').title(),
            'fq_name': ri.get_fq_name_str(),
            'uuid': ri.uuid,
        }
        msg = r'^%s$' % msg.replace('(', r'\(').replace(')', r'\)')
        with self.assertRaisesRegex(RefsExistError, msg):
            self.api.routing_instance_update(ri)

    def test_cannot_update_default_routing_instance_to_not_default(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        default_ri_fq_name = vn.fq_name + [vn.fq_name[-1]]
        default_ri = self.api.routing_instance_read(default_ri_fq_name)

        default_ri.set_routing_instance_is_default(False)
        msg = CANNOT_MODIFY_MSG % {
            'resource_type':
                RoutingInstance.object_type.replace('_', ' ').title(),
            'fq_name': default_ri.get_fq_name_str(),
            'uuid': default_ri.uuid,
        }
        msg = r'^%s$' % msg.replace('(', r'\(').replace(')', r'\)')
        with self.assertRaisesRegex(RefsExistError, msg):
            self.api.routing_instance_update(default_ri)

    def test_cannot_delete_default_routing_instance(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        default_ri_fq_name = vn.fq_name + [vn.fq_name[-1]]
        default_ri = self.api.routing_instance_read(default_ri_fq_name)

        msg = CANNOT_MODIFY_MSG % {
            'resource_type':
                RoutingInstance.object_type.replace('_', ' ').title(),
            'fq_name': default_ri.get_fq_name_str(),
            'uuid': default_ri.uuid,
        }
        msg = r'^%s$' % msg.replace('(', r'\(').replace(')', r'\)')
        with self.assertRaisesRegex(RefsExistError, msg):
            self.api.routing_instance_delete(id=default_ri.uuid)

        self.api.virtual_network_delete(id=vn.uuid)

    def test_can_update_and_delete_non_default_routing_instance(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        ri = RoutingInstance('ri-%s' % self.id(), parent_obj=vn)
        self.api.routing_instance_create(ri)

        ri.set_display_name('new-name-%s' % self.id())
        self.api.routing_instance_update(ri)

        self.api.routing_instance_delete(id=ri.uuid)

    def test_routing_instance_service_chain_info(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        ri_name = 'ri-%s' % self.id()
        ri_fq_name = ':'.join(vn.fq_name + [ri_name])

        sci = ServiceChainInfo(
            service_chain_id=ri_fq_name,
            prefix=['20.0.0.0/24'],
            routing_instance=ri_name,
            service_chain_address='0.255.255.250',
            service_instance='default-domain:default-project:test_service',
            sc_head=True)

        sciv6 = ServiceChainInfo(
            service_chain_id=ri_fq_name,
            prefix=['1000::/16'],
            routing_instance=ri_name,
            service_chain_address='::0.255.255.252',
            service_instance='default-domain:default-project:test_service_v6',
            sc_head=False)

        ri = RoutingInstance(name=ri_name,
                             parent_obj=vn,
                             service_chain_information=sci,
                             ipv6_service_chain_information=sciv6,
                             evpn_service_chain_information=sci,
                             evpn_ipv6_service_chain_information=sciv6,
                             routing_instance_is_default=False)

        uuid = self.api.routing_instance_create(ri)
        ri.set_uuid(uuid)
        ri_fq_name = vn.fq_name + [ri.name]
        ri = self.api.routing_instance_read(ri_fq_name)
        ri.set_display_name('new RI name')
        self.api.routing_instance_update(ri)

        updated_ri = self.api.routing_instance_read(id=ri.uuid)
        for attr in ['service_chain_information',
                     'ipv6_service_chain_information',
                     'evpn_service_chain_information',
                     'evpn_ipv6_service_chain_information']:
            self.assertEqual(getattr(ri, attr), getattr(updated_ri, attr))
