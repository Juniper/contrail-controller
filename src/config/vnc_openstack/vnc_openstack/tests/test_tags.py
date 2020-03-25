# Copyright 2019 Juniper Networks. All rights reserved.
import time
from unittest import skip

from vnc_api.exceptions import RefsExistError
from vnc_api.vnc_api import FloatingIp, FloatingIpPool
from vnc_api.vnc_api import IpamSubnetType, SubnetType
from vnc_api.vnc_api import LogicalRouter
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import NetworkIpam, VnSubnetsType
from vnc_api.vnc_api import Tag
from vnc_api.vnc_api import VirtualNetwork
from vnc_api.vnc_api import VirtualMachineInterface

from tests import test_case

NO_TAG = 'no_tag'
TAG_RED = 'red'
TAG_BLUE = 'blue'
TAG_GREEN = 'green'
TAG_WHITE = 'white'
ALL_TAGS = [TAG_RED, TAG_BLUE, TAG_GREEN, TAG_WHITE]


class NeutronTagsTestCase(test_case.NeutronBackendTestCase):
    def setUp(self, *args, **kwargs):
        super(NeutronTagsTestCase, self).setUp(*args, **kwargs)
        self.api = self._vnc_lib

        self.project = self._create_project()
        self.tags = self._create_tags()

    def tearDown(self):
        self._delete_tags()
        self._delete_project()
        super(NeutronTagsTestCase, self).tearDown()

    def _create_project(self):
        project_name = 'project-test-tags-{}'.format(self.id())
        try:
            self.api.project_create(Project(project_name))
        except RefsExistError:
            pass  # skip if already exist

        project_fq_name = ['default-domain', project_name]
        return self.api.project_read(fq_name=project_fq_name)

    def _delete_project(self):
        self.api.project_delete(fq_name=self.project.get_fq_name())

    def _create_tags(self):
        tags = {}
        for tag in ALL_TAGS:
            try:
                self.api.tag_create(Tag(tag_type_name='neutron_tag',
                                        tag_value=tag))
            except RefsExistError:
                pass  # skip if already exist

            tag_fq_name = ['neutron_tag={}'.format(tag)]
            tags[tag] = self.api.tag_read(fq_name=tag_fq_name)
        return tags

    def _delete_tags(self):
        for _, tag in self.tags.items():
            self.api.tag_delete(fq_name=tag.get_fq_name())

    def _get_tag(self, name):
        return self.tags[name]

    def assert_one_tag_full_match(self, resource_name, resources, tag):
        """Assert only one resource with full match single tag
        has been fetched.

        :param (str) resource_name: Name of resource to check
        :param (dict) resources: Dict with tag:vnc_resource pairs
        :param (str) tag: Name of tag to test
        """
        result = self.list_resource(
            resource_name,
            proj_uuid=self.project.uuid,
            req_filters={
                'tags': tag,
            },
        )
        # check if response is not empty
        self.assertIsNotNone(result)

        # check virtual network count
        expected_count = 1
        self.assertEqual(expected_count, len(result))
        # check if virtual network uuid match
        self.assertEqual(resources[tag].uuid, result[0]['id'])

    def assert_multiple_tags_full_match(self, resource_name, resource, tags):
        """Assert only one resource with full match multiple tags
        has been fetched.

        :param (str) resource_name: Name of resource to check
        :param (obj) resource: VNC resource object
        :param (list[str]) tags: List of tags to test
        """
        result = self.list_resource(
            resource_name,
            proj_uuid=self.project.uuid,
            req_filters={
                'tags': ','.join(tags),
            },
        )
        # check if response is not empty
        self.assertIsNotNone(result)

        # check virtual network count
        expected_count = 1
        self.assertEqual(expected_count, len(result))
        # check if virtual network uuid match
        self.assertEqual(resource.uuid, result[0]['id'])

    def assert_multiple_tags_any_match(self, resource_name, resources, tags):
        """Assert all resources with any tag match have been fetched.

        :param (str) resource_name: Name of resource to check
        :param (dict) resources: Dict with tag:vnc_resource pairs
        :param (list[str]) tags: List of tags to test
        """
        result = self.list_resource(
            resource_name,
            proj_uuid=self.project.uuid,
            req_filters={
                'tags-any': ','.join(tags),
            },
        )
        # check if response is not empty
        self.assertIsNotNone(result)

        # check virtual network count
        expected_count = len(tags)
        self.assertEqual(expected_count, len(result))
        # check if virtual network uuid match
        expected_uuids = [resources[tag].uuid for tag in tags]
        for res in result:
            self.assertIn(res['id'], expected_uuids)


class TestVirtualNetworkNeutronTags(NeutronTagsTestCase):
    def setUp(self, *args, **kwargs):
        super(TestVirtualNetworkNeutronTags, self).setUp(*args, **kwargs)
        self._pre_create_virtual_networks()

    def tearDown(self):
        self._post_delete_virtual_networks()
        super(TestVirtualNetworkNeutronTags, self).tearDown()

    def _pre_create_virtual_networks(self):
        self.vns = {}
        for tag in ALL_TAGS:
            # create
            vn = VirtualNetwork('vn-{}-{}'.format(tag, self.id()),
                                parent_obj=self.project)
            vn.add_tag(self._get_tag(tag))
            vn.uuid = self.api.virtual_network_create(vn)
            # read
            self.vns[tag] = self.api.virtual_network_read(id=vn.uuid)

        # Create one virtual network without a tag
        vn_notag = VirtualNetwork('vn-{}-{}'.format(NO_TAG, self.id()),
                                  parent_obj=self.project)
        vn_notag.uuid = self.api.virtual_network_create(vn_notag)
        self.vns[NO_TAG] = self.api.virtual_network_read(id=vn_notag.uuid)

    def _post_delete_virtual_networks(self):
        for _, vn in self.vns.items():
            self.api.virtual_network_delete(id=vn.uuid)

    def test_query_virtual_network_with_one_tag(self):
        """Query one by one, virtual networks filtering by tag."""
        for tag in ALL_TAGS:
            self.assert_one_tag_full_match(resource_name='network',
                                           resources=self.vns,
                                           tag=tag)

    def test_query_virtual_network_with_two_tags_full_match(self):
        """Query virtual networks filtering by multiple tags."""
        vn = VirtualNetwork('vn-{}_{}-{}'.format(TAG_BLUE, TAG_WHITE,
                                                 self.id()))
        vn.add_tag(self._get_tag(TAG_BLUE))
        vn.add_tag(self._get_tag(TAG_WHITE))
        vn.uuid = self.api.virtual_network_create(vn)

        self.assert_multiple_tags_full_match(resource_name='network',
                                             resource=vn,
                                             tags=[TAG_BLUE, TAG_WHITE])
        # cleanup
        self.api.virtual_network_delete(id=vn.uuid)

    def test_query_all_virtual_network_with_match_any(self):
        """Query virtual networks filtering by any tags."""
        tag_test_cases = [
            [TAG_GREEN, TAG_RED],
            [TAG_BLUE, TAG_RED, TAG_WHITE],
            ALL_TAGS,
        ]
        for tag_case in tag_test_cases:
            self.assert_multiple_tags_any_match(resource_name='network',
                                                resources=self.vns,
                                                tags=tag_case)

    @skip('Long-lasting performance test. If necessary, turn it on manually.')
    def test_query_one_tags_performance(self):
        """
        Test performance of querying VN by one tag.
        Average time is about 9.037 milliseconds per one request.
        Delta: -2.163 ms after last optimization.
        """
        start = int(round(time.time() * 1000))
        for _ in range(1000):
            self.list_resource(
                'network',
                proj_uuid=self.project.uuid,
                req_filters={
                    'tags': TAG_RED,
                },
            )
        end = int(round(time.time() * 1000))

        expected_time_milliseconds = 12700
        self.assertLessEqual(end - start, expected_time_milliseconds)

    @skip('Long-lasting performance test. If necessary, turn it on manually.')
    def test_query_multiple_tags_performance(self):
        """
        Test performance of querying VN by multiple tag (full match).
        Average time is about 6.068 ms per one request.
        Delta: -1.432 ms after last optimization.
        """
        start = int(round(time.time() * 1000))
        for _ in range(1000):
            self.list_resource(
                'network',
                proj_uuid=self.project.uuid,
                req_filters={
                    'tags': ','.join([TAG_RED, TAG_BLUE, TAG_GREEN]),
                },
            )
        end = int(round(time.time() * 1000))

        expected_time_milliseconds = 8300
        self.assertLessEqual(end - start, expected_time_milliseconds)

    @skip('Long-lasting performance test. If necessary, turn it on manually.')
    def test_query_multiple_any_tags_performance(self):
        """
        Test performance of querying VN by multiple tag (match any).
        Average time is about 14.329 milliseconds per one request.
        Delta: -4.671 ms after last optimization.
        """
        start = int(round(time.time() * 1000))
        for _ in range(1000):
            self.list_resource(
                'network',
                proj_uuid=self.project.uuid,
                req_filters={
                    'tags-any': ','.join([TAG_RED, TAG_BLUE, TAG_GREEN]),
                },
            )
        end = int(round(time.time() * 1000))

        expected_time_milliseconds = 20800
        self.assertLessEqual(end - start, expected_time_milliseconds)


class TestFloatingIpNeutronTags(NeutronTagsTestCase):
    def setUp(self, *args, **kwargs):
        super(TestFloatingIpNeutronTags, self).setUp(*args, **kwargs)
        self._pre_create_virtual_network()
        self._pre_create_floating_ips()

    def tearDown(self):
        self._post_delete_floating_ips()
        self._post_delete_virtual_network()
        super(TestFloatingIpNeutronTags, self).tearDown()

    def _pre_create_virtual_network(self):
        self.ipam = NetworkIpam('ipam-{}'.format(self.id()))
        self.ipam.uuid = self.api.network_ipam_create(self.ipam)

        self.vn = VirtualNetwork('vn-forfip-{}'.format(self.id()),
                                 parent_obj=self.project)
        self.vn.add_network_ipam(self.ipam, VnSubnetsType([
            IpamSubnetType(SubnetType("192.168.7.0", 24))
        ]))
        self.vn.uuid = self.api.virtual_network_create(self.vn)

    def _post_delete_virtual_network(self):
        self.api.virtual_network_delete(id=self.vn.uuid)
        self.api.network_ipam_delete(id=self.ipam.uuid)

    def _pre_create_floating_ips(self):
        self.fip_pool = FloatingIpPool('fip-pool-{}'.format(self.id()),
                                       self.vn)
        self.fip_pool.uuid = self.api.floating_ip_pool_create(self.fip_pool)

        self.fips = {}
        for tag in ALL_TAGS:
            # create
            fip = FloatingIp("fip-{}-{}".format(tag, self.id()), self.fip_pool)
            fip.set_project(self.project)
            fip.add_tag(self._get_tag(tag))
            fip.uuid = self.api.floating_ip_create(fip)
            # read
            self.fips[tag] = self.api.floating_ip_read(id=fip.uuid)

    def _post_delete_floating_ips(self):
        for _, fip in self.fips.items():
            self.api.floating_ip_delete(id=fip.uuid)
        self.api.floating_ip_pool_delete(id=self.fip_pool.uuid)

    def test_query_floating_ip_with_one_tag(self):
        """Query one by one, floating IPs filtering by tag."""
        for tag in ALL_TAGS:
            self.assert_one_tag_full_match(resource_name='floatingip',
                                           resources=self.fips,
                                           tag=tag)

    def test_query_floating_ip_with_two_tags_full_match(self):
        """Query floating IPs filtering by multiple tags."""
        fip = FloatingIp("fip-{}_{}-{}".format(TAG_BLUE, TAG_WHITE,
                                               self.id()), self.fip_pool)
        fip.set_project(self.project)
        fip.add_tag(self._get_tag(TAG_BLUE))
        fip.add_tag(self._get_tag(TAG_WHITE))
        fip.uuid = self.api.floating_ip_create(fip)

        self.assert_multiple_tags_full_match(resource_name='floatingip',
                                             resource=fip,
                                             tags=[TAG_BLUE, TAG_WHITE])
        # cleanup
        self.api.floating_ip_delete(id=fip.uuid)

    def test_query_all_floating_ip_with_match_any(self):
        """Query floating IPs filtering by any tags."""
        tag_test_cases = [
            [TAG_GREEN, TAG_RED],
            [TAG_BLUE, TAG_RED, TAG_WHITE],
            ALL_TAGS,
        ]
        for tag_case in tag_test_cases:
            self.assert_multiple_tags_any_match(resource_name='floatingip',
                                                resources=self.fips,
                                                tags=tag_case)


class TestRouterNeutronTags(NeutronTagsTestCase):
    def setUp(self, *args, **kwargs):
        super(TestRouterNeutronTags, self).setUp(*args, **kwargs)
        self._pre_create_logical_routers()

    def tearDown(self):
        self._post_delete_logical_routers()
        super(TestRouterNeutronTags, self).tearDown()

    def _pre_create_logical_routers(self):
        self.lrs = {}
        for tag in ALL_TAGS:
            # create
            lr = LogicalRouter('lr-{}-{}'.format(tag, self.id()),
                               parent_obj=self.project)
            lr.add_tag(self._get_tag(tag))
            lr.uuid = self.api.logical_router_create(lr)
            self.lrs[tag] = self.api.logical_router_read(id=lr.uuid)

        # Create one logical router without a tag
        lr_notag = LogicalRouter('lr-{}-{}'.format(NO_TAG, self.id()),
                                 parent_obj=self.project)
        lr_notag.uuid = self.api.logical_router_create(lr_notag)
        self.lrs[NO_TAG] = self.api.logical_router_read(id=lr_notag.uuid)

    def _post_delete_logical_routers(self):
        for _, lr in self.lrs.items():
            self.api.logical_router_delete(id=lr.uuid)

    def test_query_logical_router_with_one_tag(self):
        """Query one by one, logical routers filtering by tag."""
        for tag in ALL_TAGS:
            self.assert_one_tag_full_match(resource_name='router',
                                           resources=self.lrs,
                                           tag=tag)

    def test_query_logical_router_with_two_tags_full_match(self):
        """Query logical routers filtering by multiple tags."""
        lr = LogicalRouter("lr-{}_{}-{}".format(TAG_BLUE, TAG_WHITE,
                                                self.id()),
                           parent_obj=self.project)
        lr.add_tag(self._get_tag(TAG_BLUE))
        lr.add_tag(self._get_tag(TAG_WHITE))
        lr.uuid = self.api.logical_router_create(lr)

        self.assert_multiple_tags_full_match(resource_name='router',
                                             resource=lr,
                                             tags=[TAG_BLUE, TAG_WHITE])
        # cleanup
        self.api.logical_router_delete(id=lr.uuid)

    def test_query_all_floating_ip_with_match_any(self):
        """Query logical routers filtering by any tags."""
        tag_test_cases = [
            [TAG_GREEN, TAG_RED],
            [TAG_BLUE, TAG_RED, TAG_WHITE],
            ALL_TAGS,
        ]
        for tag_case in tag_test_cases:
            self.assert_multiple_tags_any_match(resource_name='router',
                                                resources=self.lrs,
                                                tags=tag_case)


class TestPortNeutronTags(NeutronTagsTestCase):
    def setUp(self, *args, **kwargs):
        super(TestPortNeutronTags, self).setUp(*args, **kwargs)
        self._pre_create_virtual_network()
        self._pre_create_virtual_machine_interfaces()

    def tearDown(self):
        self._post_delete_virtual_machine_interfaces()
        self._post_delete_virtual_network()
        super(TestPortNeutronTags, self).tearDown()

    def _pre_create_virtual_network(self):
        vn_uuid = self.api.virtual_network_create(
            VirtualNetwork('vn-forvmi'.format(self.id()),
                           parent_obj=self.project))
        self.vn = self.api.virtual_network_read(id=vn_uuid)

    def _post_delete_virtual_network(self):
        self.api.virtual_network_delete(id=self.vn.uuid)

    def _pre_create_virtual_machine_interfaces(self):
        self.vmis = {}
        for tag in ALL_TAGS:
            # create
            vmi = VirtualMachineInterface(
                'vmi-{}-{}'.format(tag, self.id()), parent_obj=self.project)
            vmi.set_virtual_network(self.vn)
            vmi.add_tag(self._get_tag(tag))
            vmi.uuid = self.api.virtual_machine_interface_create(vmi)
            self.vmis[tag] = self.api.virtual_machine_interface_read(
                id=vmi.uuid)

        # Create one virtual machine interface without a tag
        vmi_notag = VirtualMachineInterface(
            'vmi-{}-{}'.format(NO_TAG, self.id()),  parent_obj=self.project)
        vmi_notag.set_virtual_network(self.vn)
        vmi_notag.uuid = self.api.virtual_machine_interface_create(vmi_notag)
        self.vmis[NO_TAG] = self.api.virtual_machine_interface_read(
            id=vmi_notag.uuid)

    def _post_delete_virtual_machine_interfaces(self):
        for _, vmi in self.vmis.items():
            self.api.virtual_machine_interface_delete(id=vmi.uuid)

    def test_query_virtual_machine_interface_with_one_tag(self):
        """Query one by one, virtual machine interfaces filtering by tag."""
        for tag in ALL_TAGS:
            self.assert_one_tag_full_match(resource_name='port',
                                           resources=self.vmis,
                                           tag=tag)

    def test_query_virtual_machine_interface_with_two_tags_full_match(self):
        """Query virtual machine interfaces filtering by multiple tags."""
        vmi = VirtualMachineInterface(
            'vmi-{}_{}-{}'.format(TAG_BLUE, TAG_WHITE, self.id()),
            parent_obj=self.project)
        vmi.set_virtual_network(self.vn)
        vmi.add_tag(self._get_tag(TAG_BLUE))
        vmi.add_tag(self._get_tag(TAG_WHITE))
        vmi.uuid = self.api.virtual_machine_interface_create(vmi)

        self.assert_multiple_tags_full_match(resource_name='port',
                                             resource=vmi,
                                             tags=[TAG_BLUE, TAG_WHITE])
        # cleanup
        self.api.virtual_machine_interface_delete(id=vmi.uuid)

    def test_query_all_virtual_machine_interface_with_match_any(self):
        """Query virtual machine interfaces filtering by any tags."""
        tag_test_cases = [
            [TAG_GREEN, TAG_RED],
            [TAG_BLUE, TAG_RED, TAG_WHITE],
            ALL_TAGS,
        ]
        for tag_case in tag_test_cases:
            self.assert_multiple_tags_any_match(resource_name='port',
                                                resources=self.vmis,
                                                tags=tag_case)
