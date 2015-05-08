import bottle
from vnc_openstack import fip_res_handler as fip_handler
from vnc_openstack import subnet_res_handler as subnet_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vmi_res_handler as vmi_handler
from vnc_openstack import vn_res_handler as vn_handler


class TestFipHandlers(test_common.TestBase):
    def setUp(self):
        super(TestFipHandlers, self).setUp()
        self._handler = fip_handler.FloatingIpHandler(self._test_vnc_lib)

    def _create_external_network(self, name, proj, cidr='10.0.0.0/24'):
        vn = vn_handler.VNetworkHandler(
            self._test_vnc_lib).resource_create(
            {'name': name,
             'router:external': True,
             'tenant_id': self._uuid_to_str(self.proj_obj.uuid)})
        vn = vn['id']
        subnet_q = {'name': name,
                    'cidr': cidr,
                    'ip_version': 4,
                    'network_id': str(vn)}
        subnet_handler.SubnetHandler(
            self._test_vnc_lib).resource_create(subnet_q)

        return vn

    def test_create(self):
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)

        normal_vn = vn_handler.VNetworkHandler(
            self._test_vnc_lib).resource_create(
            {'name': 'test-net',
             'tenant_id': proj_uuid_str})
        ext_vn = self._create_external_network('public', self.proj_obj)

        entries = [{
            # with a private network
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'is_admin': False
                },
                'fip_q': {
                    'tenant_id': proj_uuid_str,
                    'floating_network_id': normal_vn['id']
                }
            },
            'output': bottle.HTTPError
        }, {
            # with a public network
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'is_admin': False
                },
                'fip_q': {
                    'tenant_id': proj_uuid_str,
                    'floating_network_id': ext_vn
                }
            },
            'output': {'id': self._generated(),
                       'floating_network_id': ext_vn,
                       'fixed_ip_address': None}
        }]
        self._test_check_create(entries)

    def _create_fip(self, ext_vn, tenant_id, port_id=None):
        res = self._handler.resource_create(
            context={'tenant': tenant_id,
                     'is_admin': True},
            fip_q={'floating_network_id': ext_vn,
                   'port_id': port_id,
                   'tenant_id': tenant_id})
        return res['id']

    def test_delete(self):
        ext_vn = self._create_external_network('public', self.proj_obj)
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)

        fip_uuid = self._create_fip(ext_vn, proj_uuid_str)
        count = self._handler.resource_count(None, {})
        self.assertEqual(count, 1)

        self._handler.resource_delete(fip_uuid)
        count = self._handler.resource_count(None, {})
        self.assertEqual(count, 0)

    def _create_test_subnet(self, name, net_uuid, cidr='192.168.1.0/24'):
        subnet_q = {'name': name,
                    'cidr': cidr,
                    'ip_version': 4,
                    'network_id': str(net_uuid)}
        ret = subnet_handler.SubnetHandler(
            self._test_vnc_lib).resource_create(subnet_q)
        subnet_uuid = ret['id']
        return subnet_uuid

    def _create_test_port(self, name, net_uuid, proj_uuid,
                          subnet_uuid=None,
                          ip_address='192.168.1.3'):
        context = {'tenant_id': self._uuid_to_str(proj_uuid),
                   'is_admin': False}
        port_q = {'name': name,
                  'network_id': str(net_uuid),
                  'tenant_id': context['tenant_id']}
        port_q['fixed_ips'] = [{'subnet_id': subnet_uuid,
                                'ip_address': ip_address}]
        res = vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_create(context, port_q)

        return res['id']

    def test_update(self):
        ext_vn = self._create_external_network('public', self.proj_obj)
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)
        proj_1 = self._project_create('proj-1')
        proj_1_uuid_str = self._uuid_to_str(proj_1.uuid)

        normal_vn = vn_handler.VNetworkHandler(
            self._test_vnc_lib).resource_create(
            {'name': 'test-net',
             'tenant_id': proj_uuid_str})
        subnet_uuid = self._create_test_subnet('test-subnet', normal_vn['id'])
        vmi_uuid = self._create_test_port('test-port', normal_vn['id'],
                                          proj_uuid_str, subnet_uuid)

        fip_uuid = self._create_fip(ext_vn, proj_uuid_str)

        entries = [{
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'is_admin': False
                },
                'fip_id': fip_uuid,
                'fip_q': {'port_id': vmi_uuid}
            },
            'output': {'fixed_ip_address': '192.168.1.3'}
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'is_admin': False
                },
                'fip_id': fip_uuid,
                'fip_q': {'port_id': vmi_uuid}
            },
            'output': bottle.HTTPError
        }]
        self._test_check_update(entries)

    def test_get(self):
        self._test_failures_on_get()
        ext_vn = self._create_external_network('public', self.proj_obj)
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)
        fip_uuid = self._create_fip(ext_vn, proj_uuid_str)

        entries = [{
            'input': fip_uuid,
            'output': {
                'id': fip_uuid
            }
        }]
        self._test_check_get(entries)

    def test_list(self):
        ext_vn = self._create_external_network('public', self.proj_obj)
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)
        proj_1 = self._project_create('proj-1')
        proj_1_uuid_str = self._uuid_to_str(proj_1.uuid)

        normal_vn = vn_handler.VNetworkHandler(
            self._test_vnc_lib).resource_create(
            {'name': 'test-net',
             'tenant_id': proj_uuid_str})
        subnet_uuid = self._create_test_subnet('test-subnet', normal_vn['id'])
        vmi_uuid = self._create_test_port('test-port', normal_vn['id'],
                                          proj_uuid_str, subnet_uuid)
        fip_1_uuid = self._create_fip(ext_vn, proj_1_uuid_str)
        fip_2_uuid = self._create_fip(ext_vn, proj_uuid_str, vmi_uuid)

        entries = [{
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'is_admin': True
                },
                'filters': {}
            },
            'output': [{'id': fip_1_uuid}, {'id': fip_2_uuid}]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'is_admin': False
                },
                'filters': {}
            },
            'output': [{'id': fip_1_uuid}]
        }, {
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'is_admin': False
                },
                'filters': {'tenant_id': [proj_uuid_str, proj_1_uuid_str]},
            },
            'output': [{'id': fip_2_uuid}]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'is_admin': True
                },
                'filters': {'port_id': [vmi_uuid]}
            },
            'output': [{'id': fip_2_uuid}]
        }, {
            'input': {
                'context': {
                    'tenant': proj_1_uuid_str,
                    'is_admin': True
                },
                'filters': {'floating_ip_address': ['10.0.0.1']}
            },
            'output': []
        }]

        self._test_check_list(entries)

    def test_count(self):
        ext_vn = self._create_external_network('public', self.proj_obj)
        proj_uuid_str = self._uuid_to_str(self.proj_obj.uuid)
        self._create_fip(ext_vn, proj_uuid_str)

        entries = [{
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'is_admin': True
                },
                'filters': {'floating_ip_address': ['10.0.0.1']}
            },
            'output': 0
        }, {
            'input': {
                'context': {
                    'tenant': proj_uuid_str,
                    'is_admin': True
                },
                'filters': {},
            },
            'output': 1
        }]
        self._test_check_count(entries)
