# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.

import json
import sys
import uuid

from keystonemiddleware import auth_token
import mock
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VncApi

from vnc_cfg_api_server.tests import test_case

sys.path.append('../common/tests')  # noqa
import test_utils  # noqa


def get_token(user_name, project_name, domain_name, role_name, project_id=None,
              domain_id=None):
    token_dict = {
        'X-User': user_name,
        'X-User-Name': user_name,
        'X-Project-Name': project_name,
        'X-Project-Id': project_id or '',
        'X-Domain-Id': domain_id or '',
        'X-Domain-Name': domain_name,
        'X-Role': role_name,
    }
    rval = json.dumps(token_dict)
    return rval


def ks_admin_authenticate(self, response=None, headers=None):
    rval = get_token('admin', 'admin', 'default-domain', 'cloud-admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers


class TestPostAuthKeystone(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls):
        extra_mocks = [
            (auth_token, 'AuthProtocol', test_utils.FakeAuthProtocol),
            (VncApi, '_authenticate', ks_admin_authenticate),
        ]
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'auth', 'keystone'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestPostAuthKeystone, cls).setUpClass(
            extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    def test_default_kestone_domain_id_replaced(self):
        admin_token = get_token(
            'admin',
            'admin',
            'Default',
            'cloud-admin',
            uuid.uuid4().hex,
            'default')
        self._vnc_lib.set_auth_token(admin_token)
        project = Project('project-%s' % self.id())
        default_domain = self._vnc_lib.domain_read(['default-domain'])
        with mock.patch('vnc_cfg_api_server.vnc_cfg_api_server.VncApiServer.'
                        'default_domain', new_callable=mock.PropertyMock) as\
                dd_prop_mock:
            dd_prop_mock.return_value = default_domain.serialize_to_json()
            self._vnc_lib.project_create(project)
            dd_prop_mock.assert_called()


class TestPostAuthKeystone2(test_case.ApiServerTestCase):
    changed_default_doamin_id = 'changed-default-domain-id'

    @classmethod
    def setUpClass(cls):
        extra_mocks = [
            (auth_token, 'AuthProtocol', test_utils.FakeAuthProtocol),
            (VncApi, '_authenticate', ks_admin_authenticate),
        ]
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'auth', 'keystone'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('KEYSTONE', 'default_domain_id', cls.changed_default_doamin_id),
        ]
        super(TestPostAuthKeystone2, cls).setUpClass(
            extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    def test_custom_default_kestone_domain_id_replaced(self):
        admin_token = get_token(
            'admin',
            'admin',
            'Default',
            'cloud-admin',
            uuid.uuid4().hex,
            self.changed_default_doamin_id)
        self._vnc_lib.set_auth_token(admin_token)
        project = Project('project-%s' % self.id())
        default_domain = self._vnc_lib.domain_read(['default-domain'])
        with mock.patch('vnc_cfg_api_server.vnc_cfg_api_server.VncApiServer.'
                        'default_domain', new_callable=mock.PropertyMock) as\
                dd_prop_mock:
            dd_prop_mock.return_value = default_domain.serialize_to_json()
            self._vnc_lib.project_create(project)
            dd_prop_mock.assert_called()
