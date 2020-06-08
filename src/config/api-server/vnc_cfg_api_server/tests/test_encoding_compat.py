# -*- coding: utf-8 -*-

from __future__ import unicode_literals

from cfgm_common.exceptions import NoIdError
from cfgm_common.utils import encode_string
from cfgm_common.utils import old_encode_string
from vnc_api.vnc_api import Project

from vnc_cfg_api_server.tests import test_case


class TestEncoding(test_case.ApiServerTestCase):
    @property
    def api(self):
        return self._vnc_lib

    def test_fq_name_encoding(self):
        test_suite = [
            'non-ascii-é',
            'only ascii with space',
            'non ascii with space é',
            'non-ascii-encoded-\xe9',
            'foo=bar',
            'foo/bar',
        ]
        for name in test_suite:
            name = '%s-%s' % (self.id(), name)
            project = Project(name=name)
            self.api.project_create(project)

            self.create_resource(project)
            self.delete_resource(project)

    def create_resource(self, project):
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')

        original_columns = fq_name_cf.get(
            project.object_type, include_timestamp=True)
        col_name = encode_string(
            ':'.join(project.get_fq_name() + [project.get_uuid()]))
        old_col_name = ':'.join([old_encode_string(project.get_fq_name_str()),
                                 project.get_uuid()])
        new_columns = {}
        for k, v in original_columns.items():
            if col_name != k:
                new_columns[k] = v
            else:
                new_columns[old_col_name] = v

        with fq_name_cf.patch_row(
                project.object_type, new_columns=new_columns):
            self.api.fq_name_to_id(project.object_type, project.get_fq_name())

    def delete_resource(self, project):
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')

        original_columns = fq_name_cf.get(
            project.object_type, include_timestamp=True)
        col_name = encode_string(
            ':'.join(project.get_fq_name() + [project.get_uuid()]))
        old_col_name = ':'.join([old_encode_string(project.get_fq_name_str()),
                                 project.get_uuid()])
        new_columns = {}
        for k, v in original_columns.items():
            if col_name != k:
                new_columns[k] = v
            else:
                new_columns[k] = v
                new_columns[old_col_name] = v

        with fq_name_cf.patch_row(
                project.object_type, new_columns=new_columns):
            import pycassa
            self.api.project_delete(id=project.get_uuid())
            self.assertRaises(
                NoIdError,
                self.api.fq_name_to_id,
                project.object_type,
                project.get_fq_name())
            self.assertRaises(
                pycassa.NotFoundException,
                fq_name_cf.get,
                project.object_type,
                columns=[col_name])
            self.assertRaises(
                pycassa.NotFoundException,
                fq_name_cf.get,
                project.object_type,
                columns=[old_col_name])
