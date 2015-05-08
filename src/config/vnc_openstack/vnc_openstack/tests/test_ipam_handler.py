from vnc_api import vnc_api
from vnc_openstack import ipam_res_handler as ipam_handler
from vnc_openstack.tests import test_common


class TestIPamHandlers(test_common.TestBase):
    def setUp(self):
        super(TestIPamHandlers, self).setUp()
        self._handler = ipam_handler.IPamHandler(self._test_vnc_lib)

    def test_create(self):
        self._test_failures_on_create(invalid_tenant=True)

        entries = [{
            'input': {
                'ipam_q': {'mgmt': {
                    'cidr_block': {
                        'ip_prefix': '10.10.10.0',
                        'ip_prefix_len': 24
                    }},
                    'name': 'test-ipam',
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid)
                }
            },
            'output': {
                'fq_name': ['default-domain', 'default-project',
                            'test-ipam'],
                'mgmt': {'ipam_method': None,
                         'ipam_dns_method': None,
                         'ipam_dns_server': None,
                         'dhcp_option_list': None,
                         'host_routes': None,
                         'cidr_block': {'ip_prefix': '10.10.10.0',
                                        'ip_prefix_len': 24}},
                'name': 'test-ipam'}
        }]
        self._test_check_create(entries)

    def _create_test_ipam(self, name, proj):
        res = self._handler.resource_create(
            {'name': name, 'tenant_id': self._uuid_to_str(proj.uuid)})
        self.assertEqual(res['mgmt'], None)
        return res['id']

    def test_update(self):
        self._test_failures_on_update()

        ipam_id = self._create_test_ipam('test-ipam', self.proj_obj)
        entries = [{
            'input': {
                'ipam_id': ipam_id,
                'ipam_q': {'mgmt': {
                    'cidr_block': {
                        'ip_prefix': '10.10.10.0',
                        'ip_prefix_len': 24
                    }}
                }
            },
            'output': {
                'fq_name': ['default-domain', 'default-project',
                            'test-ipam'],
                'mgmt': {'ipam_method': None,
                         'ipam_dns_method': None,
                         'ipam_dns_server': None,
                         'dhcp_option_list': None,
                         'host_routes': None,
                         'cidr_block': {'ip_prefix': '10.10.10.0',
                                        'ip_prefix_len': 24}},
                'name': 'test-ipam'}
        }]
        self._test_check_update(entries)

    def test_delete(self):
        ipam_id = self._create_test_ipam('test-ipam', self.proj_obj)

        self.assertEqual(self._handler.resource_count(), 1)

        self._handler.resource_delete(ipam_id)

        self.assertEqual(self._handler.resource_count(), 0)

    def test_list(self):
        ipam_id_1 = self._create_test_ipam('test-ipam-1', self.proj_obj)
        proj_1 = self._project_create('proj-1')
        ipam_id_2 = self._create_test_ipam('test-ipam-2', proj_1)

        entries = [{
            'input': {
                'context': None,
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_1.uuid), '$%^$%^']
                }
            },
            'output': [{
                'name': 'test-ipam-2',
                'id': ipam_id_2
            }]
        }, {
            'input': {
                'context': None,
                'filters': None
            },
            'output': [{
                'name': 'test-ipam-1',
                'id': str(ipam_id_1)
            }, {
                'name': 'test-ipam-2',
                'id': str(ipam_id_2)
            }]
        }]
        self._test_check_list(entries)

    def test_get(self):
        self._test_failures_on_get()

        ipam_id = self._create_test_ipam('test-ipam', self.proj_obj)
        ipam_obj = self._test_vnc_lib.resources_collection[
            'network_ipam'][ipam_id]
        vn_obj = vnc_api.VirtualNetwork('test_net', self.proj_obj)
        vn_obj.set_network_ipam(ipam_obj, None)
        self._test_vnc_lib.virtual_network_create(vn_obj)

        entries = [{
            'input': ipam_id,
            'output': {
                'name': 'test-ipam',
                'nets_using': [
                    ['default-domain',
                     'default-project',
                     'test_net']]
            }
        }]
        self._test_check_get(entries)

    def test_count(self):
        ipam_id = self._create_test_ipam('test-ipam', self.proj_obj)

        entries = [{
            'input': {
                'filters': {
                    'id': [ipam_id]
                }
            },
            'output': 1
        }, {
            'input': {
                'filters': {
                    'id': [test_common.INVALID_UUID]
                }
            },
            'output': 0
        }]
        self._test_check_count(entries)
