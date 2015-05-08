#    Copyright
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
import bottle
from cfgm_common import exceptions as vnc_exc
import mock
from vnc_api import vnc_api
from vnc_openstack import ipam_res_handler as ipam_handler
from vnc_openstack import subnet_res_handler as subnet_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vmi_res_handler as vmi_handler

import test_contrail_res_handler as test_res_handler


class TestSubnetMixin(test_res_handler.TestContrailBase):

    def setUp(self):
        super(TestSubnetMixin, self).setUp()
        self._subnet_mixin = subnet_handler.SubnetMixin()
        self._subnet_mixin._vnc_lib = self.vnc_lib

    def test__subnet_vnc_read_mapping_id(self):
        self.vnc_lib.kv_retrieve.return_value = 'foo-key'
        self.assertEqual(
            'foo-key', self._subnet_mixin._subnet_vnc_read_mapping(
                id='foo-id'))

    def test__subnet_vnc_read_mapping_id_not_exist(self):
        self.vnc_lib.kv_retrieve.side_effect = vnc_exc.NoIdError(mock.ANY)
        fake_vn_obj = mock.Mock()
        fake_subnet_vnc1 = self._get_fake_subnet_vnc('10.0.0.0', '24',
                                                     'foo-id-1')
        fake_subnet_vnc2 = self._get_fake_subnet_vnc('12.0.0.0', '24',
                                                     'foo-id-2')
        fake_ipam_subnets = mock.Mock()
        fake_ipam_subnets.get_ipam_subnets.return_value = [fake_subnet_vnc1,
                                                           fake_subnet_vnc2]
        fake_ipam_refs = [{'attr': fake_ipam_subnets}]

        fake_vn_obj.get_network_ipam_refs.return_value = fake_ipam_refs
        fake_vn_obj.uuid = 'foo-net-id'
        self._subnet_mixin._resource_list = mock.Mock()
        self._subnet_mixin._resource_list.return_value = [fake_vn_obj]
        self.assertEqual(
            'foo-net-id 10.0.0.0/24',
            self._subnet_mixin._subnet_vnc_read_mapping(id='foo-id-1'))
        self.assertRaises(bottle.HTTPError,
                          self._subnet_mixin._subnet_vnc_read_mapping, id='id')

    def test__subnet_vnc_read_mapping_key(self):
        self.vnc_lib.kv_retrieve.return_value = 'foo-id'
        self.assertEqual(
            'foo-id', self._subnet_mixin._subnet_vnc_read_mapping(
                key='foo-key'))

    def test__subnet_vnc_read_mapping_key_not_exists(self):
        self.vnc_lib.kv_retrieve.side_effect = vnc_exc.NoIdError(mock.ANY)
        fake_vn_obj = mock.Mock()
        fake_subnet_vnc1 = self._get_fake_subnet_vnc('10.0.0.0', '24',
                                                     'foo-id-1')
        fake_subnet_vnc2 = self._get_fake_subnet_vnc('12.0.0.0', '24',
                                                     'foo-id-2')
        fake_ipam_subnets = mock.Mock()
        fake_ipam_subnets.get_ipam_subnets.return_value = [fake_subnet_vnc1,
                                                           fake_subnet_vnc2]
        fake_ipam_refs = [{'attr': fake_ipam_subnets}]

        fake_vn_obj.get_network_ipam_refs.return_value = fake_ipam_refs
        fake_vn_obj.uuid = 'foo-net-id'

        self._subnet_mixin._resource_get = mock.Mock()
        self._subnet_mixin._resource_get.return_value = fake_vn_obj
        self.assertEqual(
            'foo-id-1',
            self._subnet_mixin._subnet_vnc_read_mapping(
                key='foo-net-id 10.0.0.0/24'))


class TestSubnetHandlers(test_common.TestBase):
    def setUp(self):
        super(TestSubnetHandlers, self).setUp()
        self._handler = subnet_handler.SubnetHandler(self._test_vnc_lib)

    def _create_test_subnet(self, name, net_obj, cidr='192.168.1.0/24',
                            host_routes=None):
        subnet_q = {'name': name,
                    'cidr': cidr,
                    'ip_version': 4,
                    'network_id': str(net_obj.uuid)}
        if host_routes:
            subnet_q['host_routes'] = host_routes

        ret = self._handler.resource_create(subnet_q)
        return ret['id']

    def test_create(self):
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)

        subnet_q = {'name': 'test-subnet',
                    'cidr': '192.168.1.0/24',
                    'ip_version': '4',
                    'network_id': str(net_obj.uuid)}
        output = {'name': 'test-subnet',
                  'network_id': net_obj.uuid,
                  'allocation_pools': [{'first_ip': '192.168.1.2',
                                        'last_ip': '192.168.1.254'}],
                  'gateway_ip': '192.168.1.1',
                  'ip_version': 4,
                  'cidr': '192.168.1.0/24',
                  'tenant_id': self.proj_obj.uuid.replace('-', '')}
        entries = [{'input': {
            'subnet_q': subnet_q},
            'output': output}]
        self._test_check_create(entries)

        # create a subnet which already exists.
        entries[0]['output'] = bottle.HTTPError
        self._test_check_create(entries)

    def test_create_dhcp_disabled(self):
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)

        subnet_q = {'name': 'test-subnet',
                    'cidr': '192.168.1.0/24',
                    'ip_version': '4',
                    'enable_dhcp': False,
                    'network_id': str(net_obj.uuid)}
        output = {'name': 'test-subnet',
                  'network_id': net_obj.uuid,
                  'allocation_pools': [{'first_ip': '192.168.1.2',
                                        'last_ip': '192.168.1.254'}],
                  'gateway_ip': '192.168.1.1',
                  'enable_dhcp': False,
                  'ip_version': 4,
                  'cidr': '192.168.1.0/24',
                  'tenant_id': self.proj_obj.uuid.replace('-', '')}
        entries = [{'input': {
            'subnet_q': subnet_q},
            'output': output}]
        self._test_check_create(entries)

    def test_create_allocation_pool(self):
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)

        subnet_q = {'name': 'test-subnet',
                    'cidr': '192.168.1.0/24',
                    'ip_version': '4',
                    'enable_dhcp': True,
                    'network_id': str(net_obj.uuid),
                    'allocation_pools': [{'start': '192.168.1.100',
                                          'end': '192.168.1.130'}],
                    'gateway_ip': '192.168.1.4'}
        output = {'name': 'test-subnet',
                  'network_id': net_obj.uuid,
                  'allocation_pools': [{'first_ip': '192.168.1.100',
                                        'last_ip': '192.168.1.130'}],
                  'gateway_ip': '192.168.1.4',
                  'enable_dhcp': True,
                  'ip_version': 4,
                  'cidr': '192.168.1.0/24',
                  'tenant_id': self.proj_obj.uuid.replace('-', '')}
        entries = [{'input': {
            'subnet_q': subnet_q},
            'output': output}]
        self._test_check_create(entries)

    def _create_test_ipam(self, tenant_id):
        ipam_q = {'name': 'fake-ipam',
                  'tenant_id':  tenant_id}
        ipam_dict = ipam_handler.IPamCreateHandler().resource_create(ipam_q)
        return ipam_handler._resource_get(id=ipam_dict['id'])

    def test_create_ipam_exists(self):
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_q = {'name': 'test-subnet',
                    'cidr': '192.168.1.0/24',
                    'ip_version': '4',
                    'enable_dhcp': True,
                    'network_id': str(net_obj.uuid),
                    'contrail:ipam_fq_name': ['default-domain',
                                              'default-project',
                                              'fake-ipam']}
        output = {'name': 'test-subnet',
                  'network_id': net_obj.uuid,
                  'allocation_pools': [{'first_ip': '192.168.1.2',
                                        'last_ip': '192.168.1.254'}],
                  'gateway_ip': '192.168.1.1',
                  'ip_version': 4,
                  'cidr': '192.168.1.0/24',
                  'tenant_id': self.proj_obj.uuid.replace('-', '')}
        entries = [{'input': {
            'subnet_q': subnet_q},
            'output': output}]
        self._test_check_create(entries)

    def _subnet_count_check(self, exp_count, context=None):
        entries = {'input': {'filters': None},
                   'output': exp_count}
        if context:
            entries['input']['context'] = context

        self._test_check_count([entries])

    def test_delete(self):
        context = {'tenant': self._uuid_to_str(self.proj_obj.uuid),
                   'is_admin': False}

        self._test_failures_on_delete()
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        net_obj_2 = vnc_api.VirtualNetwork('test-net-2', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj_2)

        self._create_test_subnet('test-subnet-1', net_obj)
        subnet_uuid_2 = self._create_test_subnet('test-subnet-2', net_obj,
                                                 cidr='172.168.1.0/24')
        subnet_uuid_3 = self._create_test_subnet('test-subnet-3', net_obj_2,
                                                 cidr='10.0.0.0/24')

        self._subnet_count_check(3, context=context)
        self._handler.resource_delete(subnet_uuid_2)
        self._subnet_count_check(2, context=context)
        self._handler.resource_delete(subnet_uuid_3)
        self._subnet_count_check(1, context=context)

    def test_update(self):
        self._test_failures_on_update()

        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)

        subnet_q = {'gateway_ip': '192.168.1.10'}
        self.assertRaises(bottle.HTTPError,
                          self._handler.resource_update, subnet_uuid, subnet_q)

        subnet_q = {'allocation_pools': [{'start': '192.168.1.40'}]}
        self.assertRaises(bottle.HTTPError,
                          self._handler.resource_update, subnet_uuid, subnet_q)

        subnet_q = {'enable_dhcp': False, 'name': 'new-test-subnet-name'}
        exp_output = {'name': 'new-test-subnet-name',
                      'network_id': net_obj.uuid,
                      'enable_dhcp': False}
        entries = [{'input': {'subnet_id': subnet_uuid, 'subnet_q': subnet_q},
                    'output': exp_output}]
        self._test_check_update(entries)

        subnet_q = {'host_routes': [{'destination': '20.0.0.0/24',
                                     'nexthop': '192.168.1.20'},
                                    {'destination': '40.0.0.0/24',
                                    'nexthop': '192.168.1.30'}]}
        exp_output = {'name': 'new-test-subnet-name',
                      'network_id': net_obj.uuid,
                      'enable_dhcp': False,
                      'routes': [{'destination': '20.0.0.0/24',
                                  'nexthop': '192.168.1.20',
                                  'subnet_id': subnet_uuid},
                                 {'destination': '40.0.0.0/24',
                                  'nexthop': '192.168.1.30',
                                  'subnet_id': subnet_uuid}]}
        entries = [{'input': {'subnet_id': subnet_uuid, 'subnet_q': subnet_q},
                    'output': exp_output}]
        self._test_check_update(entries)

        subnet_q = {'dns_nameservers': ['8.8.8.8', '9.9.9.9']}
        exp_output = {'name': 'new-test-subnet-name',
                      'network_id': net_obj.uuid,
                      'enable_dhcp': False,
                      'routes': [{'destination': '20.0.0.0/24',
                                  'nexthop': '192.168.1.20',
                                  'subnet_id': subnet_uuid},
                                 {'destination': '40.0.0.0/24',
                                  'nexthop': '192.168.1.30',
                                  'subnet_id': subnet_uuid}],
                      'dns_nameservers': [{'address': '8.8.8.8',
                                           'subnet_id': subnet_uuid},
                                          {'address': '9.9.9.9',
                                           'subnet_id': subnet_uuid}]}
        entries = [{'input': {'subnet_id': subnet_uuid, 'subnet_q': subnet_q},
                    'output': exp_output}]
        self._test_check_update(entries)

    def test_get(self):
        self._test_failures_on_get()

        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)

        allocation_pools = [{'first_ip': '192.168.1.2',
                             'last_ip': '192.168.1.254'}]
        entries = [{'input': subnet_uuid,
                    'output': {'name': 'test-subnet',
                               'network_id': net_obj.uuid,
                               'allocation_pools': allocation_pools,
                               'gateway_ip': '192.168.1.1',
                               'ip_version': 4,
                               'cidr': '192.168.1.0/24',
                               'tenant_id': self.proj_obj.uuid.replace('-',
                                                                       '')}
                    }]
        self._test_check_get(entries)

    def test_list(self):
        proj_1 = self._project_create('proj-1')
        net_obj_1 = vnc_api.VirtualNetwork('test-net-1', proj_1)
        self._test_vnc_lib.virtual_network_create(net_obj_1)
        self._create_test_subnet('test-subnet-1', net_obj_1)

        proj_2 = self._project_create('proj_2')
        net_obj_2 = vnc_api.VirtualNetwork('test-net-2', proj_2)
        self._test_vnc_lib.virtual_network_create(net_obj_2)
        self._create_test_subnet('test-subnet-2', net_obj_2, '192.168.2.0/24')

        entries = [
            # non admin context, with default tenant in context
            {'input': {
                'context': {
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': False},
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_1.uuid),
                                  self._uuid_to_str(proj_2.uuid)]}},
                'output': []},

            # admin context with default tenant in context
            {'input': {
                'context': {
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': True},
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_1.uuid),
                                  self._uuid_to_str(proj_2.uuid)]}},
             'output': [{'name': 'test-subnet-1'},
                        {'name': 'test-subnet-2'}]},

            # non-admin context with proj-1 and with filters of net-2 and
            # proj_1
            {'input': {
                'context': {
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': False},
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_1.uuid),
                                  self._uuid_to_str(proj_2.uuid)],
                    'network_id': [str(net_obj_2.uuid)]}},
                'output': []},

            # admin context with proj-1 and with filters of net-2 and proj_1
            {'input': {
                'context': {
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': True},
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_1.uuid),
                                  self._uuid_to_str(proj_2.uuid)],
                    'network_id': [str(net_obj_2.uuid)]}},
                'output': [{'name': 'test-subnet-2'}]}]
        self._test_check_list(entries)

    def _create_test_port(self, name, net_obj, proj_obj,
                          with_fixed_ip=False, subnet_uuid=None,
                          ip_address='192.168.1.3',
                          apply_subnet_host_routes=False):
        context = {'tenant_id': self._uuid_to_str(proj_obj.uuid),
                   'is_admin': False}
        port_q = {'name': name,
                  'network_id': str(net_obj.uuid),
                  'tenant_id': context['tenant_id']}

        if with_fixed_ip:
            port_q['fixed_ips'] = [{'subnet_id': subnet_uuid,
                                    'ip_address': ip_address}]

        _vmi_handler = vmi_handler.VMInterfaceHandler(self._test_vnc_lib)
        _vmi_handler.resource_create(
            context, port_q, apply_subnet_host_routes=apply_subnet_host_routes)

        context['tenant'] = context['tenant_id']
        res = _vmi_handler.resource_list(context, filters={'name': name})
        self.assertEqual(len(res), 1)
        return res[0]['id']

    def _delete_test_port(self, port_id):
        _vmi_handler = vmi_handler.VMInterfaceHandler(self._test_vnc_lib)
        _vmi_handler.resource_delete(port_id)

    def _get_iface_route_tables_count(self):
        iface_rt_tables = self._test_vnc_lib.interface_route_tables_list(
            detail=True)
        return len(iface_rt_tables)

    def test_subnet_apply_host_routes(self):
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        host_routes = [{'destination': '10.0.0.0/24',
                        'nexthop': '192.168.1.10'},
                       {'destination': '10.0.0.0/24',
                        'nexthop': '192.168.1.20'}]
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj,
                                               host_routes=host_routes)

        port_1 = self._create_test_port('test-port-1', net_obj=net_obj,
                                        proj_obj=self.proj_obj,
                                        with_fixed_ip=True,
                                        ip_address='192.168.1.10',
                                        subnet_uuid=subnet_uuid,
                                        apply_subnet_host_routes=True)
        iface_rt_table_count = self._get_iface_route_tables_count()
        self.assertEqual(1, iface_rt_table_count)

        port_2 = self._create_test_port('test-port-2', net_obj=net_obj,
                                        proj_obj=self.proj_obj,
                                        with_fixed_ip=True,
                                        ip_address='192.168.1.20',
                                        subnet_uuid=subnet_uuid,
                                        apply_subnet_host_routes=True)

        iface_rt_table_count = self._get_iface_route_tables_count()
        self.assertEqual(2, iface_rt_table_count)

        subnet_q = {'host_routes': [{'destination': '20.0.0.0/24',
                                     'nexthop': '192.168.1.20'},
                                    {'destination': '40.0.0.0/24',
                                    'nexthop': '192.168.1.30'}]}

        self._handler.resource_update(subnet_uuid, subnet_q,
                                      apply_subnet_host_routes=True)

        iface_rt_table_count = self._get_iface_route_tables_count()
        self.assertEqual(1, iface_rt_table_count)

        self._delete_test_port(port_1)
        iface_rt_table_count = self._get_iface_route_tables_count()
        self.assertEqual(1, iface_rt_table_count)

        port_3 = self._create_test_port('test-port-3', net_obj=net_obj,
                                        proj_obj=self.proj_obj,
                                        with_fixed_ip=True,
                                        ip_address='192.168.1.30',
                                        subnet_uuid=subnet_uuid,
                                        apply_subnet_host_routes=True)

        iface_rt_table_count = self._get_iface_route_tables_count()
        self.assertEqual(2, iface_rt_table_count)

        self._delete_test_port(port_2)
        self._delete_test_port(port_3)

        iface_rt_table_count = self._get_iface_route_tables_count()
        self.assertEqual(0, iface_rt_table_count)
