import bottle
import vnc_openstack
from vnc_openstack import sg_res_handler as sg_handler
from vnc_openstack.tests import test_common


class TestSecurityGroupHandlers(test_common.TestBase):
    def setUp(self):
        super(TestSecurityGroupHandlers, self).setUp()
        self._handler = sg_handler.SecurityGroupHandler(self._test_vnc_lib)

    def test_create(self):
        self._test_failures_on_create(invalid_tenant=True)

        entries = [{
            'input': {
                'sg_q': {
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                    'name': 'default',
                    'description': 'Test Security Group'
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sg_q': {
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                    'name': 'test-sg',
                    'description': 'Test Security Group'
                }
            },
            'output': {
                'name': 'test-sg',
                'description': 'Test Security Group'
            }
        }, {
            'input': {
                'sg_q': {
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                    'name': 'sg-without-desc'
                }
            },
            'output': {
                'name': 'sg-without-desc',
                'description': None
            }
        }]
        self._test_check_create(entries)

    def test_delete(self):
        vnc_openstack.ensure_default_security_group(
            self._test_vnc_lib, self.proj_obj)

        sg_res = self._test_vnc_lib.resources_collection['security_group']
        self.assertTrue('default-domain:default-project:default' in sg_res)
        default_uuid = sg_res['default-domain:default-project:default'].uuid

        proj_1 = self._project_create('proj-1')

        entries = [{
            'input': {
                'context': {
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': False
                },
                'sg_id': default_uuid
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'context': {
                    'tenant_id': self._uuid_to_str(proj_1.uuid),
                    'is_admin': True
                },
                'sg_id': default_uuid
            },
            'output': None
        }]
        self._test_check_delete(entries)

    def test_update(self):
        self._test_failures_on_update()
        vnc_openstack.ensure_default_security_group(
            self._test_vnc_lib, self.proj_obj)
        sg_res = self._test_vnc_lib.resources_collection['security_group']
        self.assertTrue('default-domain:default-project:default' in sg_res)
        default_uuid = sg_res['default-domain:default-project:default'].uuid

        entries = [{
            'input': {
                'sg_id': default_uuid,
                'sg_q': {
                    'description': 'Default SG modified'
                }
            },
            'output': {
                'description': 'Default SG modified'
            }
        }]
        self._test_check_update(entries)

    def _create_test_sg(self, name, proj):
        sg_q = {
            'tenant_id': self._uuid_to_str(proj.uuid),
            'name': name,
        }

        res = self._handler.resource_create(sg_q)
        return res['id']

    def test_list(self):
        proj_1 = self._project_create('proj_1')
        proj_1_uuid_str = self._uuid_to_str(proj_1.uuid)
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)

        sg_uuid = self._create_test_sg('test-sg', self.proj_obj)

        entries = [{
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'tenant_id': self.proj_obj.uuid,
                    'is_admin': False
                },
                'filters': {}
            },
            'output': [{
                'name': 'default'
            }, {
                'name': 'test-sg'
            }]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'tenant_id': proj_1.uuid,
                    'is_admin': False
                },
                'filters': {}
            },
            'output': [{
                'name': 'default'
            }]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'tenant_id': proj_1.uuid,
                    'is_admin': True,
                },
                'filters': {'name': 'default'},
                'contrail_extensions_enabled': True
            },
            'output': [{
                'id': self._generated(),
                'contrail:fq_name': ['default-domain',
                                     'default-project',
                                     'default']
            }, {
                'id': self._generated(),
                'contrail:fq_name': ['default-domain',
                                     'proj_1',
                                     'default']
            }]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'tenant_id': proj_1.uuid,
                    'is_admin': True,
                },
                'filters': {'id': sg_uuid}
            },
            'output': [{
                'id': sg_uuid,
                'name': 'test-sg'
            }]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'tenant_id': proj_1.uuid,
                    'is_admin': True,
                },
                'filters': {'tenant_id': [proj_1.uuid]},
                'contrail_extensions_enabled': True
            },
            'output': [{
                'contrail:fq_name': ['default-domain',
                                     'proj_1',
                                     'default'],
                'name': 'default'
            }]
        }]
        self._test_check_list(entries)

    def test_get(self):
        self._test_failures_on_get()

        sg_uuid = self._create_test_sg('test-sg', self.proj_obj)

        entries = [{
            'input': sg_uuid,
            'output': {
                'id': sg_uuid,
                'name': 'test-sg'
            }
        }]
        self._test_check_get(entries)
