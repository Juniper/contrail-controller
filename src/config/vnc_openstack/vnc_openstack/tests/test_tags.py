# Copyright 2019 Juniper Networks. All rights reserved.
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import Tag
from vnc_api.vnc_api import VirtualNetwork

from tests import test_case


class TestNeutronTags(test_case.NeutronBackendTestCase):
    TAG_RED = 'red'
    TAG_BLUE = 'blue'
    TAG_GREEN = 'green'
    TAG_LIST = [TAG_RED, TAG_BLUE, TAG_GREEN]

    def setUp(self, *args, **kwargs):
        super(TestNeutronTags, self).setUp(*args, **kwargs)
        self.api = self._vnc_lib

        # create project
        project_id = self.api.project_create(
            Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=project_id)

        # Pre-create tags for later use.
        self._tags = {}
        for tag in self.TAG_LIST:
            tag_id = self.api.tag_create(Tag(tag_type_name='neutron_tag',
                                             tag_value=tag))
            self._tags[tag] = self.api.tag_read(id=tag_id)

    def _get_tag(self, name):
        return self._tags[name]

    def test_query_virtual_network_with_one_tag(self):
        # Create one virtual network for each TAG from TAG_LIST.
        # Assign the tag to VN, and store VN in the vns map for assertions.
        vns = {}
        for tag in self.TAG_LIST:
            # create
            vn = VirtualNetwork('vn-{}-{}'.format(tag, self.id()))
            vn.add_tag(self._get_tag(tag))
            vn.uuid = self.api.virtual_network_create(vn)
            # read
            vns[tag] = self.api.virtual_network_read(id=vn.uuid)

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
        expected_vn_count = 2
        self.assertEqual(expected_vn_count, len(result))

        # check if result match request filter
        expected_vn_uuids = [
            vns[self.TAG_RED].uuid,
            vns[self.TAG_BLUE].uuid,
        ]
        for vn in result:
            self.assertIn(vn['id'], expected_vn_uuids)
