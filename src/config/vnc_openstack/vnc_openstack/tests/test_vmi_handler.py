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

import contextlib
import uuid

import bottle
import mock
from neutron.common import constants as n_constants
from vnc_api import vnc_api
from vnc_openstack import subnet_res_handler as subnet_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vmi_res_handler as vmi_handler

import test_contrail_res_handler as test_res_handler


class TestVmiHandlers(test_common.TestBase):
    def setUp(self):
        super(TestVmiHandlers, self).setUp()
        self._handler = vmi_handler.VMInterfaceHandler(self._test_vnc_lib)

    def test_create(self):
        context = {'is_admin': False,
                   'tenant_id': self._uuid_to_str(self.proj_obj.uuid)}

        # test with invalid network_id
        port_q_invalid_network_id = {
            'network_id': test_common.INVALID_UUID,
            'tenant_id': self._uuid_to_str(self.proj_obj.uuid)}
        entries = [{'input': {
            'context': context,
            'port_q': port_q_invalid_network_id},
            'output': bottle.HTTPError}]
        self._test_check_create(entries)

        # test with invalid tenant_id
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)

        context['is_admin'] = True
        port_q_invalid_tenant_id = {
            'network_id': str(net_obj.uuid),
            'tenant_id': test_common.INVALID_UUID}
        entries = [{'input': {
            'context': context,
            'port_q': port_q_invalid_tenant_id},
            'output': bottle.HTTPError}]
        self._test_check_create(entries)

        # test with mismatching tenant_ids
        context['is_admin'] = False
        self._test_check_create(entries)

        # create one success entry
        proj_1 = self._project_create(name='proj-1')
        context['tenant_id'] = self._uuid_to_str(proj_1.uuid)
        port_q = {'name': 'test-port-1',
                  'network_id': str(net_obj.uuid),
                  'tenant_id': context['tenant_id']}
        entries = [{'input': {
            'context': context,
            'port_q': port_q},
            'output': {'name': 'test-port-1',
                       'network_id': str(net_obj.uuid),
                       'mac_address': self._generated()}}]
        self._test_check_create(entries)

        # create with the same mac id
        vmis = self._test_vnc_lib.virtual_machine_interfaces_list()[
            'virtual-machine-interfaces']
        self.assertEqual(len(vmis), 1)
        mac = vmis[0]['virtual_machine_interface_mac_addresses'].mac_address[0]

        port_q['mac_address'] = mac
        port_q.pop('name')
        entries = [{'input': {
            'context': context,
            'port_q': port_q},
            'output': bottle.HTTPError}]
        self._test_check_create(entries)

        # create a test subnet
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)

        # check with a different mac and a fixed ip address
        port_q['mac_address'] = mac.replace('0', '1')
        port_q['fixed_ips'] = [{'subnet_id': subnet_uuid,
                               'ip_address': '192.168.1.3'}]
        entries = [{'input': {
            'context': context,
            'port_q': port_q},
            'output': {'mac_address': port_q['mac_address'],
                       'fixed_ips': [{'subnet_id': subnet_uuid,
                                      'ip_address': '192.168.1.3'}]}}]
        self._test_check_create(entries)

        # try creating a port with same fixed ip as before
        port_q['mac_address'] = mac.replace('0', '2')
        entries[0]['output'] = bottle.HTTPError
        self._test_check_create(entries)

    def _create_test_subnet(self, name, net_obj, cidr='192.168.1.0/24'):
        subnet_q = {'name': name,
                    'cidr': cidr,
                    'ip_version': 4,
                    'network_id': str(net_obj.uuid)}
        ret = subnet_handler.SubnetHandler(
            self._test_vnc_lib).resource_create(subnet_q)
        subnet_uuid = ret['id']
        return subnet_uuid

    def _create_test_port(self, name, net_obj, proj_obj,
                          with_fixed_ip=False, subnet_uuid=None,
                          ip_address='192.168.1.3'):
        context = {'tenant_id': self._uuid_to_str(proj_obj.uuid),
                   'is_admin': False}
        port_q = {'name': name,
                  'network_id': str(net_obj.uuid),
                  'tenant_id': context['tenant_id']}

        exp_output = {'name': name,
                      'network_id': net_obj.uuid}
        if with_fixed_ip:
            port_q['fixed_ips'] = [{'subnet_id': subnet_uuid,
                                    'ip_address': ip_address}]
            exp_output['fixed_ips'] = [{'subnet_id': subnet_uuid,
                                        'ip_address': ip_address}]
        entries = [{'input': {
            'context': context,
            'port_q': port_q},
            'output': exp_output}]
        self._test_check_create(entries)

        context['tenant'] = context['tenant_id']
        res = self._handler.resource_list(context, filters={'name': name})
        self.assertEqual(len(res), 1)
        return res[0]['id']

    def _port_count_check(self, exp_count):
        entries = {'input': {'filters': None},
                   'output': exp_count}

        self._test_check_count([entries])

    def test_delete(self):
        self._test_failures_on_delete()

        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)

        # create a port
        port_id_1 = self._create_test_port('test-port-1',
                                           net_obj=net_obj,
                                           proj_obj=self.proj_obj,
                                           with_fixed_ip=True,
                                           subnet_uuid=subnet_uuid)
        self._create_test_port('test-port-2',
                               net_obj=net_obj,
                               proj_obj=self.proj_obj,
                               with_fixed_ip=False)
        self._port_count_check(2)

        self._handler.resource_delete(port_id_1)

        self._port_count_check(1)

    def test_update(self):
        self._test_failures_on_update()

        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)

        # create a port
        port_id_1 = self._create_test_port('test-port-1',
                                           net_obj=net_obj,
                                           proj_obj=self.proj_obj,
                                           with_fixed_ip=True,
                                           subnet_uuid=subnet_uuid)
        self._port_count_check(1)

        vm_uuid = str(uuid.uuid4())
        # update certain params and check
        entries = [{'input': {
            'port_q': {
                'name': 'test-port-updated',
                'admin_state_up': False,
                'security_groups': [],
                'device_owner': 'vm',
                'device_id': vm_uuid,
                'fixed_ips': [{'subnet_id': subnet_uuid,
                               'ip_address': '192.168.1.10'}],
                'allowed_address_pairs': [{'ip_address': "10.0.0.0/24"},
                                          {'ip_address': "192.168.1.4"}],
                'extra_dhcp_opts': [{'opt_name': '4',
                                     'opt_value': '8.8.8.8'}]},
            'port_id': port_id_1},
            'output': {'name': 'test-port-updated',
                       'admin_state_up': False,
                       'id': port_id_1,
                       'device_id': vm_uuid,
                       'device_owner': '',
                       'security_groups': [self._generated()],
                       'extra_dhcp_opts': [{'opt_value': '8.8.8.8',
                                            'opt_name': '4'}],
                       'allowed_address_pairs': [
                           {'ip_address': '10.0.0.0/24'},
                           {'ip_address': '192.168.1.4'}]}}]
        self._test_check_update(entries)

        sg_rules = vnc_api.PolicyEntriesType()
        sg_obj = vnc_api.SecurityGroup(
            name='test-sec-group',
            parent_obj=self.proj_obj,
            security_group_entries=sg_rules)
        self._test_vnc_lib.security_group_create(sg_obj)
        vm_uuid_2 = str(uuid.uuid4())
        entries[0]['input']['port_q']['security_groups'] = [sg_obj.uuid]
        entries[0]['input']['port_q']['device_id'] = vm_uuid_2
        entries[0]['input']['port_q'][
            'fixed_ips'][0]['ip_address'] = '192.168.1.11'
        entries[0]['output']['security_groups'] = [sg_obj.uuid]
        entries[0]['output']['device_id'] = vm_uuid_2
        entries[0]['output']['fixed_ips'] = [{'ip_address': '192.168.1.11'}]

        self._test_check_update(entries)

    def test_get(self):
        self._test_failures_on_get()

        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)

        # create a port
        port_id_1 = self._create_test_port('test-port-1',
                                           net_obj=net_obj,
                                           proj_obj=self.proj_obj,
                                           with_fixed_ip=True,
                                           subnet_uuid=subnet_uuid)
        entries = [{'input': port_id_1,
                    'output': {
                        'mac_address': self._generated(),
                        'fixed_ips': [{'subnet_id': subnet_uuid,
                                       'ip_address': '192.168.1.3'}],
                        'name': 'test-port-1',
                        'admin_state_up': True}}]
        self._test_check_get(entries)

    def test_list(self):
        proj_1 = self._project_create('proj-1')
        net_obj_1 = vnc_api.VirtualNetwork('test-net-1', proj_1)
        self._test_vnc_lib.virtual_network_create(net_obj_1)
        subnet_uuid_1 = self._create_test_subnet('test-subnet-1', net_obj_1)
        self._create_test_port('test-port-1',
                               net_obj_1,
                               proj_obj=proj_1,
                               with_fixed_ip=True,
                               subnet_uuid=subnet_uuid_1)

        proj_2 = self._project_create('proj_2')
        net_obj_2 = vnc_api.VirtualNetwork('test-net-2', proj_2)
        self._test_vnc_lib.virtual_network_create(net_obj_2)
        subnet_uuid_2 = self._create_test_subnet(
            'test-subnet-2', net_obj_2, '192.168.2.0/24')
        self._create_test_port('test-port-2',
                               net_obj_2,
                               proj_obj=proj_2,
                               with_fixed_ip=True,
                               subnet_uuid=subnet_uuid_2,
                               ip_address='192.168.2.3')

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
                'output': [{'name': 'test-port-1'}, {'name': 'test-port-2'}]},

            # non-admin context with proj1 and with filters of net2 and proj1
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
                'output': [{'name': 'test-port-2'}]}]
        self._test_check_list(entries)

    def test_count(self):
        proj_1 = self._project_create('proj-1')
        net_obj_1 = vnc_api.VirtualNetwork('test-net-1', proj_1)
        self._test_vnc_lib.virtual_network_create(net_obj_1)
        subnet_uuid_1 = self._create_test_subnet('test-subnet-1', net_obj_1)
        self._create_test_port('test-port-1',
                               net_obj_1,
                               proj_obj=proj_1,
                               with_fixed_ip=True,
                               subnet_uuid=subnet_uuid_1)

        proj_2 = self._project_create('proj_2')
        net_obj_2 = vnc_api.VirtualNetwork('test-net-2', proj_2)
        self._test_vnc_lib.virtual_network_create(net_obj_2)
        subnet_uuid_2 = self._create_test_subnet(
            'test-subnet-2', net_obj_2, '192.168.2.0/24')
        self._create_test_port('test-port-2',
                               net_obj_2,
                               proj_obj=proj_2,
                               with_fixed_ip=True,
                               subnet_uuid=subnet_uuid_2,
                               ip_address='192.168.2.3')
        entries = [{
            'input': {'filters': {
                'network_id': str(net_obj_2.uuid),
                'tenant_id': self._uuid_to_str(proj_2.uuid)}},
            'output': 1}]
        self._test_check_count(entries)


class TestVMInterfaceMixin(test_res_handler.TestContrailBase):

    def setUp(self):
        super(TestVMInterfaceMixin, self).setUp()
        self.vmi_mixin = vmi_handler.VMInterfaceMixin()
        self.vmi_mixin._vnc_lib = self.vnc_lib

    def test__port_fixed_ips_is_present(self):
        check = {'ip_address': ['10.0.0.3', '10.0.0.4', '10.0.0.5']}
        against = [{'ip_address': '10.0.0.4'}]
        self.assertTrue(self.vmi_mixin._port_fixed_ips_is_present(
            check, against))
        against = [{'ip_address': '20.0.0.32'}]
        self.assertFalse(self.vmi_mixin._port_fixed_ips_is_present(
            check, against))

    def test__get_vmi_memo_req_dict(self):
        vn_objs = [mock.Mock(), mock.Mock()]
        vn_objs[0].uuid = 'vn-1'
        vn_objs[1].uuid = 'vn-2'

        iip_obj = mock.Mock()
        iip_obj.uuid = 'iip-1'

        vm_objs = [mock.Mock(), mock.Mock(), mock.Mock()]
        vm_objs[0].uuid = 'vm-1'
        vm_objs[1].uuid = 'vm-2'
        vm_objs[2].uuid = 'vm-3'

        expected_memo_dict = {}
        expected_memo_dict['networks'] = {'vn-1': vn_objs[0],
                                          'vn-2': vn_objs[1]}
        expected_memo_dict['subnets'] = {'vn-1': [{'id': 'sn-1',
                                                   'cidr': '10.0.0.0/24'}],
                                         'vn-2': [{'id': 'sn-2',
                                                   'cidr': '20.0.0.0/24'}]}
        expected_memo_dict['instance-ips'] = {'iip-1': iip_obj}
        expected_memo_dict['virtual-machines'] = {'vm-1': vm_objs[0],
                                                  'vm-2': vm_objs[1],
                                                  'vm-3': vm_objs[2]}

        def _fake_get_vn_subnets(vn_obj):
            if vn_obj.uuid == 'vn-1':
                return [{'id': 'sn-1', 'cidr': '10.0.0.0/24'}]
            elif vn_obj.uuid == 'vn-2':
                return [{'id': 'sn-2', 'cidr': '20.0.0.0/24'}]

        with mock.patch.object(subnet_handler.SubnetHandler,
                               'get_vn_subnets') as mock_get_vns:
            mock_get_vns.side_effect = _fake_get_vn_subnets
            returned_memo_dict = self.vmi_mixin._get_vmi_memo_req_dict(
                vn_objs, [iip_obj], vm_objs)
            self.assertEqual(expected_memo_dict, returned_memo_dict)

    def test__get_extra_dhcp_opts_none(self):
        vmi_obj = mock.Mock()
        vmi_obj.get_virtual_machine_interface_dhcp_option_list.return_value = (
            None)
        self.assertIsNone(self.vmi_mixin._get_extra_dhcp_opts(vmi_obj))

    def test__get_extra_dhcp_opts(self):
        dhcp_opt_list = mock.Mock()
        dhcp_opts = [mock.Mock(), mock.Mock()]
        dhcp_opts[0].dhcp_option_value = 'value-1'
        dhcp_opts[0].dhcp_option_name = 'name-1'
        dhcp_opts[1].dhcp_option_value = 'value-2'
        dhcp_opts[1].dhcp_option_name = 'name-2'

        dhcp_opt_list.dhcp_option = dhcp_opts
        expected_dhcp_opt_list = [{'opt_value': 'value-1',
                                   'opt_name': 'name-1'},
                                  {'opt_value': 'value-2',
                                   'opt_name': 'name-2'}]

        vmi_obj = mock.Mock()
        vmi_obj.get_virtual_machine_interface_dhcp_option_list.return_value = (
            dhcp_opt_list)

        returned_dhcp_opt_list = self.vmi_mixin._get_extra_dhcp_opts(vmi_obj)
        self.assertEqual(expected_dhcp_opt_list, returned_dhcp_opt_list)

    def test__get_allowed_adress_pairs(self):
        allowed_addr_pairs = mock.Mock()
        allowed_addr_pair_list = [mock.MagicMock(), mock.MagicMock()]
        allowed_addr_pair_list[0].mac = 'mac-1'
        allowed_addr_pair_list[0].ip.get_ip_prefix_len.return_value = 32
        allowed_addr_pair_list[0].ip.get_ip_prefix.return_value = (
            '10.0.10.10')
        allowed_addr_pair_list[1].mac = 'mac-2'
        allowed_addr_pair_list[1].ip.get_ip_prefix_len.return_value = 24
        allowed_addr_pair_list[1].ip.get_ip_prefix.return_value = (
            '20.0.0.0')
        allowed_addr_pairs.allowed_address_pair = allowed_addr_pair_list
        expected_allowed_addr_paris = [{'mac_address': 'mac-1',
                                        'ip_address': '10.0.10.10'},
                                       {'mac_address': 'mac-2',
                                        'ip_address': '20.0.0.0/24'}]
        vmi_obj = mock.Mock()
        addr_pair = vmi_obj.get_virtual_machine_interface_allowed_address_pairs
        addr_pair.return_value = allowed_addr_pairs

        returned_addr_pairs = self.vmi_mixin._get_allowed_adress_pairs(vmi_obj)
        self.assertEqual(expected_allowed_addr_paris, returned_addr_pairs)

    def test__get_allowed_adress_pairs_none(self):
        vmi_obj = mock.Mock()
        addr_pair = vmi_obj.get_virtual_machine_interface_allowed_address_pairs
        addr_pair.return_value = None
        self.assertIsNone(self.vmi_mixin._get_allowed_adress_pairs(vmi_obj))

    def test__ip_address_to_subnet_id_none(self):
        memo_req = {'subnets': {}}
        vn_obj = mock.Mock()
        vn_obj.get_network_ipam_refs.return_value = []
        self.assertIsNone(self.vmi_mixin._ip_address_to_subnet_id('10.0.0.4',
                                                                  vn_obj,
                                                                  memo_req))

    def test__ip_address_to_subnet_id_from_memo_req(self):
        vn_obj = mock.Mock()
        vn_obj.uuid = 'vn-fake-id'
        memo_req = {'subnets': {'vn-fake-id':
                                [{'id': 'foo-id', 'cidr': '10.0.0.0/24'}]}}
        subnet_id = self.vmi_mixin._ip_address_to_subnet_id('10.0.0.5',
                                                            vn_obj,
                                                            memo_req)
        self.assertEqual('foo-id', subnet_id)

    def test__ip_address_to_subnet_id_from_vn_obj(self):
        memo_req = {'subnets': {}}
        fake_vn_obj = mock.Mock()

        fake_subnet_vnc = self._get_fake_subnet_vnc('10.0.0.0', '24', 'foo-id')
        fake_ipam_subnets = mock.Mock()
        fake_ipam_subnets.get_ipam_subnets.return_value = [fake_subnet_vnc]
        fake_ipam_refs = [{'attr': fake_ipam_subnets}]

        fake_vn_obj.get_network_ipam_refs.return_value = fake_ipam_refs
        subnet_id = self.vmi_mixin._ip_address_to_subnet_id('10.0.0.5',
                                                            fake_vn_obj,
                                                            memo_req)
        self.assertEqual('foo-id', subnet_id)
        self.assertIsNone(self.vmi_mixin._ip_address_to_subnet_id('20.0.0.5',
                                                                  fake_vn_obj,
                                                                  memo_req))

    def test_get_vmi_ip_dict_none(self):
        self.assertEqual([], self.vmi_mixin.get_vmi_ip_dict(mock.ANY,
                                                            mock.ANY,
                                                            mock.ANY))

    def test_get_vmi_ip_dict(self):
        fake_ip_back_refs = [{'uuid': 'fake-ip-1'}, {'uuid': 'fake-ip-2'}]
        fake_vmi_obj = mock.Mock()
        fake_vmi_obj.instance_ip_back_refs = fake_ip_back_refs

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

        fake_ip_obj1 = mock.Mock()
        fake_ip_obj1.get_instance_ip_address.return_value = '10.0.0.5'

        fake_ip_obj2 = mock.Mock()
        fake_ip_obj2.get_instance_ip_address.return_value = '12.0.0.25'

        self.vnc_lib.instance_ip_read.return_value = fake_ip_obj2

        fake_port_memo = {'instance-ips': {'fake-ip-1': fake_ip_obj1},
                          'subnets': {}}

        expected_ip_dict_list = [{'ip_address': '10.0.0.5',
                                  'subnet_id': 'foo-id-1'},
                                 {'ip_address': '12.0.0.25',
                                  'subnet_id': 'foo-id-2'}]

        returned_ip_dict_list = self.vmi_mixin.get_vmi_ip_dict(
            fake_vmi_obj, fake_vn_obj, fake_port_memo)
        self.assertEqual(expected_ip_dict_list, returned_ip_dict_list)

    def _test__vmi_to_neutron_port_helper(self, allowed_pairs=None,
                                          extensions_enabled=True):
        fake_vmi_obj = mock.Mock()
        fake_vmi_obj.display_name = 'fake-port'
        fake_vmi_obj.uuid = 'fake-port-uuid'
        fake_vmi_obj.get_fq_name.return_value = ['fake-domain', 'fake-proj',
                                                 'fake-net-id']

        fake_vmi_obj.get_virtual_network_refs.return_value = [{'uuid':
                                                               'fake-net-id'}]
        fake_vmi_obj.parent_type = ''
        parent_id = str(uuid.uuid4())
        fake_vmi_obj.parent_uuid = parent_id

        fake_mac_refs = mock.Mock()
        fake_mac_refs.mac_address = ['01:02:03:04:05:06']
        fake_mac = fake_vmi_obj.get_virtual_machine_interface_mac_addresses
        fake_mac.return_value = fake_mac_refs

        fake_vmi_obj.get_security_group_refs.return_value = [{'uuid': 'sg-1'},
                                                             {'uuid': 'sg-2'}]
        fake_id_perms = mock.Mock()
        fake_id_perms.enable = True

        fake_vmi_obj.get_id_perms.return_value = fake_id_perms
        fake_vmi_obj.logical_router_back_refs = [{'uuid': 'fake-router-id'}]

        fake_vn_obj = mock.Mock()
        fake_vn_obj.parent_uuid = parent_id
        fake_subnet_vnc = self._get_fake_subnet_vnc('10.0.0.0', '24', 'foo-id')
        fake_ipam_subnets = mock.Mock()
        fake_ipam_subnets.get_ipam_subnets.return_value = [fake_subnet_vnc]
        fake_ipam_refs = [{'attr': fake_ipam_subnets}]

        fake_vn_obj.get_network_ipam_refs.return_value = fake_ipam_refs
        self.vnc_lib.virtual_network_read.return_value = fake_vn_obj

        expected_port_dict = {}
        expected_port_dict['name'] = 'fake-port'
        expected_port_dict['id'] = 'fake-port-uuid'
        expected_port_dict['tenant_id'] = parent_id.replace('-', '')
        expected_port_dict['network_id'] = 'fake-net-id'
        expected_port_dict['mac_address'] = '01:02:03:04:05:06'
        expected_port_dict['extra_dhcp_opts'] = 'extra-dhcp-opts'
        if allowed_pairs:
            expected_port_dict['allowed_address_pairs'] = allowed_pairs
        expected_port_dict['fixed_ips'] = [{'ip_address': '10.0.0.4',
                                            'subnet_id': 'fake-subnet-id'}]
        expected_port_dict['security_groups'] = ['sg-1', 'sg-2']
        expected_port_dict['admin_state_up'] = True
        expected_port_dict['device_id'] = 'fake-device-id'
        expected_port_dict['device_owner'] = 'fake-owner'
        expected_port_dict['status'] = n_constants.PORT_STATUS_ACTIVE
        if extensions_enabled:
            expected_port_dict['contrail:fq_name'] = ['fake-domain',
                                                      'fake-proj',
                                                      'fake-net-id']

        with contextlib.nested(
            mock.patch.object(self.vmi_mixin, '_get_extra_dhcp_opts'),
            mock.patch.object(self.vmi_mixin, '_get_allowed_adress_pairs'),
            mock.patch.object(self.vmi_mixin, 'get_vmi_ip_dict'),
            mock.patch.object(self.vmi_mixin, '_get_vmi_device_id_owner')
        ) as (fake_dhcp_opts, fake_addr_pairs, fake_get_vmi_ip_dict,
              fake_device_id_owner):
            fake_dhcp_opts.return_value = 'extra-dhcp-opts'
            fake_addr_pairs.return_value = allowed_pairs
            fake_get_vmi_ip_dict.return_value = (
                [{'ip_address': '10.0.0.4', 'subnet_id': 'fake-subnet-id'}])
            fake_device_id_owner.return_value = ('fake-device-id',
                                                 'fake-owner')
            returned_port_dict = self.vmi_mixin._vmi_to_neutron_port(
                fake_vmi_obj, extensions_enabled=extensions_enabled)
            self.assertEqual(expected_port_dict, returned_port_dict)

    def test__vmi_to_neutron_port(self):
        self._test__vmi_to_neutron_port_helper()

    def test__vmi_to_neutron_port_allowed_addr_pairs(self):
        self._test__vmi_to_neutron_port_helper(
            allowed_pairs='fake-allowed-pairs')

    def test__vmi_to_neutron_port_extensions_disabled(self):
        self._test__vmi_to_neutron_port_helper(extensions_enabled=False)
