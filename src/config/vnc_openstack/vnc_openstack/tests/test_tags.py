# Copyright 2019 Juniper Networks. All rights reserved.
from vnc_api.vnc_api import Project

from tests import test_case


class TestNeutronTags(test_case.NeutronBackendTestCase):
    def test_tag_query_request(self):
        project_id = self._vnc_lib.project_create(
            Project('project-%s' % self.id()))
        project = self._vnc_lib.project_read(id=project_id)

        list_result = self.list_resource(
            'network',
            proj_uuid=project_id,
            req_filters={
                'tags': 'red,blue',
            },
        )

        self.assertEqual(True, list_result)
