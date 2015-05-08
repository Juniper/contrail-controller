from vnc_api import vnc_api
from vnc_openstack import svc_instance_res_handler as svc_instance_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vn_res_handler as vn_handler


class TestSvcInstanceHandlers(test_common.TestBase):
    def setUp(self):
        super(TestSvcInstanceHandlers, self).setUp()
        self._handler = svc_instance_handler.SvcInstanceHandler(
            self._test_vnc_lib)
        self._test_vnc_lib.service_template_create(
            vnc_api.ServiceTemplate('nat-template'))

    def _create_external_network(self):
        net_q = {
            'name': 'public',
            'router:external': True,
            'tenant_id': self.proj_obj.uuid
        }
        r = vn_handler.VNetworkHandler(
            self._test_vnc_lib).resource_create(net_q)
        return r['id']

    def test_create(self):
        self._test_failures_on_create(invalid_tenant=True)
        ext_net_id = self._create_external_network()

        entries = [{
            'input': {
                'si_q': {
                    'name': 'test-svc',
                    'tenant_id': self.proj_obj.uuid,
                    'external_net': ext_net_id
                }
            },
            'output': {
                'fq_name': ['default-domain', 'default-project', 'test-svc'],
                'service_instance_properties': {
                    'right_virtual_network':
                        'default-domain:default-project:public',
                    'right_ip_address': None,
                    'scale_out': {'auto_scale': False, 'max_instances': 1},
                    'left_ip_address': None, 'left_virtual_network': ''
                }
            }
        }]
        self._test_check_create(entries)

    def _create_test_svc_instance(self, name, proj):
        ext_net = self._create_external_network()

        si_q = {
            'name': 'test-svc',
            'tenant_id': proj.uuid,
            'external_net': ext_net
        }

        res = self._handler.resource_create(si_q)
        return res['uuid']

    def test_delete(self):
        svc_id = self._create_test_svc_instance('test-svc', self.proj_obj)

        self.assertTrue(svc_id in self._test_vnc_lib.resources_collection[
            'service_instance'])
        self._handler.resource_delete(svc_id)

        self.assertEqual(len(self._test_vnc_lib.resources_collection[
            'service_instance']), 0)

    def test_list(self):
        proj_1 = self._project_create('proj-1')
        self._create_test_svc_instance('test-svc', self.proj_obj)

        entries = [{
            'input': {
                'context': {
                    'tenant_id': proj_1.uuid,
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': False
                },
                'filters': {
                    'id': [test_common.INVALID_UUID]
                }
            },
            'output': []
        }, {
            'input': {
                'context': {
                    'tenant_id': self.proj_obj,
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': True
                },
                'filters': {
                    'name': ['test-svc']
                }
            },
            'output': [{
                'name': 'test-svc'
            }]
        }, {
            'input': {
                'context': {
                    'tenant_id': self.proj_obj,
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': True
                },
                'filters': {
                    'tenant_id': [self.proj_obj.uuid]
                }
            },
            'output': [{
                'name': 'test-svc'
            }]
        }, {
            'input': {
                'context': {
                    'tenant_id': self.proj_obj,
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': True
                },
                'filters': {
                }
            },
            'output': [{
                'name': 'test-svc'
            }]
        }]
        self._test_check_list(entries)

    def test_get(self):
        self._test_failures_on_get()
