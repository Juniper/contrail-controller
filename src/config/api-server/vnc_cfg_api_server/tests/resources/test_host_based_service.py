#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import json
import logging

from vnc_api.exceptions import BadRequest
from vnc_api.exceptions import OverQuota
from vnc_api.gen.resource_client import HostBasedService
from vnc_api.gen.resource_client import Project
from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
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

    def test_hbs_mgmnt_vn_ref_muliple_add(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn1)
        vn2 = VirtualNetwork('vn2-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn2)

        for vn_type in ['management']:
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
                vn2.resource_type,
                vn2.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )

    def test_hbs_mgmnt_vn_ref_add_del_add(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn1)
        vn2 = VirtualNetwork('vn2-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn2)

        for vn_type in ['management']:
            self.api.ref_update(
                hbs.resource_type,
                hbs.uuid,
                vn1.resource_type,
                vn1.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )
            self.api.ref_update(
                hbs.resource_type,
                hbs.uuid,
                vn1.resource_type,
                vn1.uuid,
                None,
                'DELETE',
                ServiceVirtualNetworkType(vn_type),
            )
            self.api.ref_update(
                hbs.resource_type,
                hbs.uuid,
                vn2.resource_type,
                vn2.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )

    def test_hbs_default_vn_ref_add(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn1)

        for vn_type in ['left', 'right']:
            self.assertRaises(
                BadRequest,
                self.api.ref_update,
                hbs.resource_type,
                hbs.uuid,
                vn1.resource_type,
                vn1.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )

    def test_hbs_default_vn_ref_del(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)
        hbs = self.api.host_based_service_read(id=hbs.uuid)
        hbs.set_virtual_network_list([])
        self.assertRaises(
            BadRequest,
            self.api.host_based_service_update,
            hbs)

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

    def get_hbs_template(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))

        kvp_array = []
        kvp = KeyValuePair("namespace", "k8test")
        kvp_array.append(kvp)
        kvp = KeyValuePair("cluster", "c1")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)

        project2.set_annotations(kvps)

        self.api.project_create(project2)
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=project2)
        self.api.host_based_service_create(hbs)

        (code, msg) = self._http_post('/hbs-get',
                                      json.dumps({'hbs_fq_name': hbs.fq_name}))
        self.assertEquals(code, 200)
        return json.loads(msg)

    def test_get_hbs_template_with_uuid(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))

        kvp_array = []
        kvp = KeyValuePair("namespace", "k8test")
        kvp_array.append(kvp)
        kvp = KeyValuePair("cluster", "c1")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)

        project2.set_annotations(kvps)

        self.api.project_create(project2)
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=project2)
        self.api.host_based_service_create(hbs)

        (code, msg) = self._http_post('/hbs-get',
                                      json.dumps({'hbs_uuid': hbs.uuid}))
        self.assertEquals(code, 200)

        return json.loads(msg)

    def test_get_hbs_template_no_json(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))

        kvp_array = []
        kvp = KeyValuePair("namespace", "k8test")
        kvp_array.append(kvp)
        kvp = KeyValuePair("cluster", "c1")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)

        project2.set_annotations(kvps)

        self.api.project_create(project2)
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=project2)
        self.api.host_based_service_create(hbs)

        (code, msg) = self._http_post('/hbs-get', None)
        self.assertEquals(code, 400)

    def test_host_based_resource_http_post(self):
        self.get_hbs_template()

    def test_hbs_template_types(self):
        hbs = self.get_hbs_template()
        left = hbs['hbs'][0]
        right = hbs['hbs'][1]
        ds = hbs['hbs'][2]

        self.assertEqual(left['kind'], 'NetworkAttachmentDefinition')
        self.assertEqual(right['kind'], 'NetworkAttachmentDefinition')
        self.assertEqual(ds['kind'], 'DaemonSet')

    def test_hbs_create_with_vn_ref(self):
        project2 = Project('project2-%s' % self.id())
        self.api.project_create(project2)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=project2)
        self.api.virtual_network_create(vn1)
        hbs1 = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        hbs1.add_virtual_network(vn1, ServiceVirtualNetworkType('left'))
        self.assertRaises(
            BadRequest,
            self.api.host_based_service_create,
            hbs1
        )

    def test_hbs_create_with_mgmt_vn_ref(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))
        self.api.project_create(project2)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=project2)
        self.api.virtual_network_create(vn1)
        hbs1 = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        hbs1.add_virtual_network(vn1, ServiceVirtualNetworkType('management'))
        self.api.host_based_service_create(hbs1)

    def test_hbs_create_with_annotations(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))
        self.api.project_create(project2)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=project2)
        self.api.virtual_network_create(vn1)
        hbs1 = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        hbs1.add_annotations(KeyValuePair(
            key='image', value='hub.juniper.net/security/csrx:19.2R1.8'))
        hbs1.add_annotations(KeyValuePair(key='imagePullSecrets', value='psd'))
        self.api.host_based_service_create(hbs1)

    def test_hbs_default_vn_ref_add_with_annotations(self):
        hbs = HostBasedService('hbs-%s' % self.id(), parent_obj=self.project)
        self.api.host_based_service_create(hbs)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=self.project)
        self.api.virtual_network_create(vn1)

        hbs.add_annotations(KeyValuePair(key='imagePullSecrets', value='psd'))
        for vn_type in ['left', 'right']:
            self.assertRaises(
                BadRequest,
                self.api.ref_update,
                hbs.resource_type,
                hbs.uuid,
                vn1.resource_type,
                vn1.uuid,
                None,
                'ADD',
                ServiceVirtualNetworkType(vn_type),
            )

    def test_hbs_create_with_annotations_and_default_left_vn(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))
        self.api.project_create(project2)
        vn1 = VirtualNetwork('vn1-%s' % self.id(), parent_obj=project2)
        self.api.virtual_network_create(vn1)
        hbs1 = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        hbs1.add_annotations(KeyValuePair(
            key='image', value='hub.juniper.net/security/csrx:19.2R1.8'))
        hbs1.add_annotations(KeyValuePair(key='imagePullSecrets', value='psd'))
        hbs1.add_virtual_network(vn1, ServiceVirtualNetworkType('left'))
        self.assertRaises(
            BadRequest,
            self.api.host_based_service_create,
            hbs1
        )

    def test_hbs_update_property(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))
        self.api.project_create(project2)
        hbs = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        self.api.host_based_service_create(hbs)
        hbs.set_display_name('new display name')
        self.api.host_based_service_update(hbs)

    def test_hbs_update_default_vn_property(self):
        project2 = Project('project2-%s' % self.id())
        project2.set_quota(QuotaType(host_based_service=1))
        self.api.project_create(project2)
        hbs = HostBasedService('hbs1-%s' % self.id(), parent_obj=project2)
        self.api.host_based_service_create(hbs)
        hbs = self.api.host_based_service_read(id=hbs.uuid)
        vn_ref_left_uuid = hbs.virtual_network_refs[0]['uuid']
        self.assertRaises(
            BadRequest,
            self.api.ref_update,
            hbs.resource_type,
            hbs.uuid,
            'virtual_network',
            vn_ref_left_uuid,
            None,
            'ADD',
            ServiceVirtualNetworkType('management'),
        )
