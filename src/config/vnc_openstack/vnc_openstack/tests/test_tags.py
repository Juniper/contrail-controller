# Copyright 2019 Juniper Networks. All rights reserved.
from vnc_api.exceptions import RefsExistError
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import Tag
from vnc_api.vnc_api import VirtualNetwork

from tests import test_case


class TestNeutronTags(test_case.NeutronBackendTestCase):
    TAG_RED = 'red'
    TAG_BLUE = 'blue'
    TAG_GREEN = 'green'
    TAG_WHITE = 'white'

    def setUp(self, *args, **kwargs):
        super(TestNeutronTags, self).setUp(*args, **kwargs)
        self.api = self._vnc_lib

        # create project
        project_id = self.api.project_create(
            Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=project_id)

        # Pre-create tags for later use.
        self._pre_create_tags()
        self._pre_create_virtual_networks()

    def tearDown(self):
        for vn in self.vns:
            self.api.virtual_network_delete(vn)
        super(TestNeutronTags, self).tearDown()

    def _get_tag(self, name):
        return self._tags[name]

    def _pre_create_tags(self):
        tag_list = [self.TAG_RED, self.TAG_BLUE,
                    self.TAG_GREEN, self.TAG_WHITE]

        self._tags = {}
        for tag in tag_list:
            try:
                tag_id = self.api.tag_create(Tag(tag_type_name='neutron_tag',
                                                 tag_value=tag))
                self._tags[tag] = self.api.tag_read(id=tag_id)
            except RefsExistError:
                self._tags[tag] = self.api.tag_read(
                    fq_name=['neutron_tag={}'.format(tag)])

    def _pre_create_virtual_networks(self):
        # Create one virtual network for each TAG from TAG_LIST.
        # Assign the tag to VN, and store VN in the vns map for assertions.
        self.vns = {}
        for tag in [self.TAG_RED, self.TAG_BLUE, self.TAG_GREEN]:
            # create
            vn = VirtualNetwork('vn-{}-{}'.format(tag, self.id()))
            vn.add_tag(self._get_tag(tag))
            vn.uuid = self.api.virtual_network_create(vn)
            # read
            self.vns[tag] = self.api.virtual_network_read(id=vn.uuid)

        # Create one virtual network without a tag
        self.api.virtual_network_create(
            VirtualNetwork('vn-notag-{}'.format(self.id())))

    def test_query_virtual_network_with_one_tag(self):
        result = self.list_resource(
            'network',
            proj_uuid=self.project.uuid,
            req_filters={
                'tags': self.TAG_RED,
            },
        )
        # check if response is not empty
        self.assertIsNotNone(result)

        # check virtual network count
        expected_vn_count = 1
        self.assertEqual(expected_vn_count, len(result))
        # check if virtual network uuid match
        self.assertEqual(self.vns[self.TAG_RED].uuid, result[0]['id'])

    def test_query_virtual_network_with_two_tags(self):
        # create virtual network with two tags
        vn = VirtualNetwork('vn-{}_{}-{}'.format(self.TAG_BLUE,
                                                 self.TAG_WHITE,
                                                 self.id()))
        vn.add_tag(self._get_tag(self.TAG_BLUE))
        vn.add_tag(self._get_tag(self.TAG_WHITE))
        vn.uuid = self.api.virtual_network_create(vn)

        result = self.list_resource(
            'network',
            proj_uuid=self.project.uuid,
            req_filters={
                'tags': ','.join([self.TAG_RED, self.TAG_BLUE]),
            },
        )
        # check if response is not empty
        self.assertIsNotNone(result)

        # check virtual network count
        expected_vn_count = 1
        self.assertEqual(expected_vn_count, len(result))
        # check if virtual network uuid match
        self.assertEqual(vn.uuid, result[0]['id'])

    def test_query_any_virtual_network_with_the_tag(self):
        result = self.list_resource(
            'network',
            proj_uuid=self.project.uuid,
            req_filters={
                'tags-any': ','.join([self.TAG_RED, self.TAG_BLUE,
                                      self.TAG_GREEN]),
            },
        )
        # check if response is not empty
        self.assertIsNotNone(result)

        # check virtual network count
        expected_vn_count = 3
        self.assertEqual(expected_vn_count, len(result))
        # check if virtual network uuid match
        expected_vn_uuids = [
            self.vns[self.TAG_RED].uuid,
            self.vns[self.TAG_BLUE].uuid,
            self.vns[self.TAG_GREEN].uuid,
        ]
        for vn in result:
            self.assertIn(vn['id'], expected_vn_uuids)
