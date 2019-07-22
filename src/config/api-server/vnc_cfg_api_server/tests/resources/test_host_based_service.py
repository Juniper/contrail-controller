#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import logging

from vnc_api.exceptions import BadRequest
from vnc_api.exceptions import OverQuota
from vnc_api.gen.resource_client import HostBasedService
from vnc_api.gen.resource_client import Project
from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import QuotaType
from vnc_api.gen.resource_xsd import ServiceVirtualNetworkType

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestHostBasedService(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestHostBasedService, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestHostBasedService, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def setUp(self):
        super(TestHostBasedService, self).setUp()
        project = Project('project-%s' % self.id())
        project.set_quota(QuotaType(host_based_service=1))
        self.api.project_create(project)
        self.project = self.api.project_read(id=project.uuid)

    def test_default_host_based_service_type(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)

        self.assertIsNotNone(hbs.get_host_based_service_type())
        self.assertEqual(hbs.get_host_based_service_type(), 'firewall')

    def test_cannot_change_host_based_service_type(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)

        HostBasedService.prop_field_types['host_based_service_type'][
            'restrictions'].append('foo')
        hbs.set_host_based_service_type('foo')
        self.assertRaises(BadRequest, self.api.host_based_service_update, hbs)

    def test_hbs_unique_vn_ref_per_type(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn1)
        vn2 = VirtualNetwork('vn2-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn2)

        for vn_type in ['management', 'left', 'right', 'other']:
            self.api.ref_update(
                hbs.resource_type,
                hbs.uuid,
                vn1.resource_type,
                vn1.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )
            self.assertRaises(
                BadRequest,
                self.api.ref_update,
                hbs.resource_type,
                hbs.uuid,
                vn1.resource_type,
                vn2.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )
            hbs = self.api.host_based_service_read(id=hbs.uuid)
            hbs.set_virtual_network_list([])
            self.api.host_based_service_update(hbs)

    def test_host_based_resource_limited_to_one_per_project(self):
        project2 = Project('project2-%s' % self.id())
        self.api.project_create(project2)

        hbs1 = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        self.assertRaises(OverQuota, self.api.host_based_service_create, hbs1)

        project2.set_quota(QuotaType(host_based_service=1))
        self._vnc_lib.project_update(project2)

        hbs1 = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        self.api.host_based_service_create(hbs1)

        hbs2 = HostBasedService('hbs2-%s' % self.id(), parent_obj=project2)
        self.assertRaises(OverQuota, self.api.host_based_service_create, hbs2)

        project2.set_quota(QuotaType(host_based_service=2))
        self.assertRaises(BadRequest, self._vnc_lib.project_update, project2)

        project2.set_quota(QuotaType(host_based_service=-1))
        self.assertRaises(BadRequest, self._vnc_lib.project_update, project2)

        project2.set_quota(QuotaType(host_based_service=None))
        self._vnc_lib.project_update(project2)
        self.assertRaises(OverQuota, self.api.host_based_service_create, hbs2)
