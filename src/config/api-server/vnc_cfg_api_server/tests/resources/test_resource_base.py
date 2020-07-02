from __future__ import absolute_import

import logging

from vnc_api.vnc_api import Project

from vnc_cfg_api_server.tests import test_case

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestResourceBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestResourceBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestResourceBase, cls).tearDownClass(*args, **kwargs)

    def test_resource_locate_with_create(self):
        test_id = self.id()
        missing_uuid = '81d3c7f6-3028-47e8-b70a-eeeeeeeeeeee'

        # create project
        proj_name = 'test_proj_%s' % test_id
        project = Project(proj_name)
        self._vnc_lib.project_create(project)
        project = self._vnc_lib.project_read(id=project.uuid)

        cls = self._api_server.get_resource_class('virtual_network')
        ok, result = cls.locate(
            uuid=missing_uuid, create_it=True)
        self.assertTrue('without at least a FQ name' in result[1])
