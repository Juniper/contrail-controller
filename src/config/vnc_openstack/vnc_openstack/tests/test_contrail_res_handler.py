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
from cfgm_common import exceptions as vnc_exc
import mock
import testtools
from vnc_api import vnc_api
from vnc_openstack import contrail_res_handler as res_handler


class TestContrailBase(testtools.TestCase):

    def setUp(self):
        super(TestContrailBase, self).setUp()
        self.vnc_lib = mock.MagicMock()

    def _get_fake_subnet_vnc(self, prefix, len, subnet_id):
        fake_subnet_vnc = mock.Mock()
        fake_subnet_vnc.subnet.get_ip_prefix.return_value = prefix
        fake_subnet_vnc.subnet.get_ip_prefix_len.return_value = len
        fake_subnet_vnc.subnet_uuid = subnet_id
        return fake_subnet_vnc


class TestContrailResHandler(TestContrailBase):

    def setUp(self):
        super(TestContrailResHandler, self).setUp()
        self.contrail_res_handler = res_handler.ContrailResourceHandler(
            self.vnc_lib)

    def test__project_read(self):
        proj_id = 'foo_id'
        fq_name = ['foo', 'bar']
        self.contrail_res_handler._project_read(proj_id=proj_id,
                                                fq_name=fq_name)
        self.vnc_lib.project_read.assert_called_once_with(id=proj_id,
                                                          fq_name=fq_name)

    def test__project_list_domain(self):
        expected_proj_list = ['foo', 'bar']
        self.vnc_lib.projects_list.return_value = {'projects':
                                                   expected_proj_list}
        returned_proj_list = self.contrail_res_handler._project_list_domain('')
        self.assertEqual(expected_proj_list, returned_proj_list)


class TestResourceCreateHandler(TestContrailBase):
    def setUp(self):
        super(TestResourceCreateHandler, self).setUp()
        self.create_handler = res_handler.ResourceCreateHandler(self.vnc_lib)
        self.create_handler.resource_create_method = 'foo_create'

    def test__resource_create(self):
        self.vnc_lib.foo_create.return_value = 'foo-uuid'
        uuid = self.create_handler._resource_create(mock.ANY)
        self.assertEqual('foo-uuid', uuid)

    def _test__resource_create_already_exists(self):
        # TODO(nusiddiq)
        def foo_create(obj):
            raise vnc_exc.RefsExistError()

        self.vnc_lib.foo_create.side_effect = vnc_exc.RefsExistError(mock.ANY)
        obj = mock.Mock()
        obj.name = 'foo-name'
        obj.uuid = 'foo-uuid'
        obj.fq_name = ['default-domain', 'project', 'foo-name']
        uuid = self.create_handler._resource_create(obj)
        self.assertRaises(cfgm_common.exceptions.RefsExistError,
                          self.create_handler._resource_create, obj)

    def test__resource_create_permission_denied(self):
        self.vnc_lib.foo_create.side_effect = vnc_exc.PermissionDenied
        self.assertRaises(bottle.HTTPError,
                          self.create_handler._resource_create, mock.ANY)


class TestResourceDeleteHandler(TestContrailBase):
    def setUp(self):
        super(TestResourceDeleteHandler, self).setUp()
        self.delete_handler = res_handler.ResourceDeleteHandler(self.vnc_lib)
        self.delete_handler.resource_delete_method = 'foo_delete'

    def test__resource_delete(self):
        self.delete_handler._resource_delete(id='foo-id', fq_name='foo-name')
        self.vnc_lib.foo_delete.assert_called_once_with(id='foo-id',
                                                        fq_name='foo-name')


class TestResourceUpdateHandler(TestContrailBase):
    def setUp(self):
        super(TestResourceUpdateHandler, self).setUp()
        self.update_handler = res_handler.ResourceUpdateHandler(self.vnc_lib)
        self.update_handler.resource_update_method = 'foo_update'

    def test__resource_update(self):
        self.update_handler._resource_update(mock.ANY)
        self.vnc_lib.foo_update.assert_called_once_with(mock.ANY)


class TestResourceGetHandler(TestContrailBase):
    def setUp(self):
        super(TestResourceGetHandler, self).setUp()
        self.get_handler = res_handler.ResourceGetHandler(self.vnc_lib)
        self.get_handler.resource_get_method = 'foo_get'
        self.get_handler.resource_list_method = 'foos_list'
        self.get_handler.detail = True

    def test__resource_list_back_ref(self):
        self.get_handler.back_ref_fields = ['foo-back-ref']
        self.vnc_lib.foos_list.return_value = ['foo-list']
        res_list = self.get_handler._resource_list(back_refs=True,
                                                   fields=['foo-field'])
        self.vnc_lib.foos_list.assert_called_once_with(
            fields=['foo-field', 'foo-back-ref'], detail=True)
        self.assertEqual(['foo-list'], res_list)

    def test__resource_list_detail(self):
        self.get_handler._resource_list(back_refs=False, detail=False)
        self.vnc_lib.foos_list.assert_called_once_with(detail=False)

    def test__resource_get_back_refs(self):
        self.get_handler.back_ref_fields = ['foo-back-ref']
        self.vnc_lib.foo_get.return_value = 'foo-get'
        ret_value = self.get_handler._resource_get(back_refs=True, foo='foo',
                                                   fields=['foo-field'])
        self.vnc_lib.foo_get.assert_called_once_with(
            fields=['foo-field', 'foo-back-ref'], foo='foo')
        self.assertEqual('foo-get', ret_value)

    def test__resource_get_method_true(self):
        self.get_handler._resource_get(resource_get_method='bar_method')
        self.vnc_lib.bar_method.assert_called_once_with(fields=[])

    def test__resource_count_optimized_no_tenant_id(self):
        ret_value = self.get_handler._resource_count_optimized({'foo': 'bar'})
        self.assertEqual(None, ret_value)

    def test__resource_count_optimized(self):
        tenant_ids = [str(uuid.uuid4()), str(uuid.uuid4())]

        def _fake_foos_list(**kwargs):
            expected_args = {'count': True,
                             'detail': False}
            if kwargs['parent_id'] in tenant_ids:
                expected_args['parent_id'] = kwargs['parent_id']

            self.assertEqual(expected_args, kwargs)
            return {'foos': {'count': 4}}

        self.vnc_lib.foos_list.side_effect = _fake_foos_list

        res_count = self.get_handler._resource_count_optimized(
            {'tenant_id': tenant_ids})
        self.assertEqual(8, res_count)

    def test__resource_count_optimized_fip(self):
        tenant_ids = [str(uuid.uuid4()), str(uuid.uuid4())]
        self.get_handler.resource_list_method = 'floating_ips_list'

        def _fake_fip_list(**kwargs):
            expected_args = {'count': True,
                             'detail': False}
            if kwargs['back_ref_id'] in tenant_ids:
                expected_args['back_ref_id'] = kwargs['back_ref_id']

            self.assertEqual(expected_args, kwargs)
            return {'floating-ips': {'count': 4}}

        self.vnc_lib.floating_ips_list.side_effect = _fake_fip_list

        res_count = self.get_handler._resource_count_optimized(
            {'tenant_id': tenant_ids})
        self.assertEqual(8, res_count)


class TestVMachineHandler(TestContrailBase):
    def setUp(self):
        super(TestVMachineHandler, self).setUp()
        self.vm_handler = res_handler.VMachineHandler(self.vnc_lib)

    def _test_ensure_vm_instance_helper(self, instance_name,
                                        get_side_effect=None,
                                        get_ret_val=None):

        self.assertEqual(instance_name, vm_obj.name)

    def test_ensure_vm_instance_present(self):
        self.vm_handler._resource_create = mock.Mock()
        self.vm_handler._resource_get = mock.Mock()
        self.vm_handler._resource_create.side_effect = (
            vnc_exc.RefsExistError(mock.ANY))
        instance_id = str(uuid.uuid4())
        vm_obj = self.vm_handler.ensure_vm_instance(instance_id)
        self.assertTrue(self.vm_handler._resource_create.called)
        self.assertTrue(self.vm_handler._resource_get.called)

    def test_ensure_vm_instance_not_present(self):
        self.vm_handler._resource_create = mock.Mock()
        instance_id = str(uuid.uuid4())
        vm_obj = self.vm_handler.ensure_vm_instance(instance_id)
        self.assertTrue(self.vm_handler._resource_create.called)


class TestInstanceIpHandler(TestContrailBase):
    def setUp(self):
        super(TestInstanceIpHandler, self).setUp()
        self.ip_handler = res_handler.InstanceIpHandler(self.vnc_lib)

    def test_is_ip_addr_in_net_id(self):
        net_id = 'foo-net-id'
        ip_addr = '10.0.0.4'

        fake_ip_obj = mock.Mock()
        fake_ip_obj.get_instance_ip_address.side_effect = (
            ['10.0.0.3', '10.0.0.4', '10.0.0.8'])
        self.ip_handler._resource_list = mock.Mock()
        self.ip_handler._resource_list.return_value = [fake_ip_obj,
                                                       fake_ip_obj,
                                                       fake_ip_obj]
        self.assertTrue(self.ip_handler.is_ip_addr_in_net_id(ip_addr, net_id))
        self.ip_handler._resource_list.assert_called_once_with(
            back_ref_id=[net_id])

    def test_create_instance_ip(self):
        self.vnc_lib.instance_ip_create.return_value = 'foo-id'
        with contextlib.nested(
            mock.patch.object(vnc_api.InstanceIp, 'set_instance_ip_address'),
            mock.patch.object(vnc_api.InstanceIp, 'set_subnet_uuid')
        ) as (fake_set_iip, fake_set_subnet):
            iip_uuid = self.ip_handler.create_instance_ip(mock.Mock(),
                                                          mock.Mock(),
                                                          ip_addr='10.0.0.3')
            self.assertEqual('foo-id', iip_uuid)
            fake_set_iip.assert_called_once_with('10.0.0.3')
            self.assertFalse(fake_set_subnet.called)
