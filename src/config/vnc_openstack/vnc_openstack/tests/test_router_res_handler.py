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
from vnc_api import vnc_api
from vnc_openstack import router_res_handler as router_handler
from vnc_openstack import subnet_res_handler as subnet_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vmi_res_handler as vmi_handler
from vnc_openstack import vn_res_handler as vn_handler


class TestLogicalRouterHandler(test_common.TestBase):
    def setUp(self):
        super(TestLogicalRouterHandler, self).setUp()
        self._handler = router_handler.LogicalRouterHandler(self._test_vnc_lib)

    def _create_external_net(self):
        net_q = {'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                 'router:external': True,
                 'shared': True,
                 'name': 'public'
                 }

        net_res_q = (vn_handler.VNetworkHandler(
            self._test_vnc_lib).resource_create(net_q))

        # create service instance template
        svc_obj = vnc_api.ServiceTemplate()
        svc_obj.fq_name = router_handler.SNAT_SERVICE_TEMPLATE_FQ_NAME

        self._test_vnc_lib.service_template_create(svc_obj)
        return net_res_q['id']

    def _create_test_router(self, name, admin_state_up=True, tenant_id=None):
        if not tenant_id:
            tenant_id = self._uuid_to_str(self.proj_obj.uuid)

        router_q = {'name': name,
                    'admin_state_up': admin_state_up,
                    'tenant_id': tenant_id}
        return self._handler.resource_create(router_q)['id']

    def _router_count_check(self, exp_count, context=None):
        entries = {'input': {'filters': None},
                   'output': exp_count}
        if context:
            entries['input']['context'] = context

        self._test_check_count([entries])

    def test_create(self):
        tenant_id = self._uuid_to_str(self.proj_obj.uuid)
        external_net_id = self._create_external_net()

        entries = [{'input': {'router_q': {'name': 'test-router-1',
                                           'admin_state_up': True,
                                           'tenant_id': tenant_id}},
                    'output': {'name': 'test-router-1', 'admin_state_up': True,
                               'tenant_id': tenant_id, 'status': 'ACTIVE'}},
                   {'input': {'router_q': {'name': 'test-router-2',
                                           'admin_state_up': False,
                                           'tenant_id': tenant_id}},
                    'output': {'name': 'test-router-2',
                               'admin_state_up': False,
                               'tenant_id': tenant_id, 'status': 'ACTIVE'}},
                   {'input': {'router_q': {'name': 'test-router-3',
                                           'admin_state_up': True,
                                           'tenant_id': tenant_id,
                                           'external_gateway_info':
                                           {'network_id': external_net_id}}},
                    'output': {'name': 'test-router-3',
                               'admin_state_up': True,
                               'tenant_id': tenant_id, 'status': 'ACTIVE',
                               'external_gateway_info': {'network_id':
                                                         external_net_id}}}]
        self._test_check_create(entries)

    def test_delete(self):
        rtr_1 = self._create_test_router('router-1')
        rtr_2 = self._create_test_router('router-2', admin_state_up=False)
        self._router_count_check(2)
        self._handler.resource_delete(rtr_1)
        self._router_count_check(1)
        self._handler.resource_delete(rtr_2)
        self._router_count_check(0)

    def test_update(self):
        rtr_id = self._create_test_router('router-1')
        router_q = {'name': 'new-router'}
        entries = [{'input': {'router_q': router_q, 'rtr_id': rtr_id},
                    'output': {'name': 'new-router'}}]
        self._test_check_update(entries)

        # set the gw
        ext_net_id = self._create_external_net()
        router_q['external_gateway_info'] = {'network_id': ext_net_id}
        entries = [{'input': {'router_q': router_q, 'rtr_id': rtr_id},
                    'output': {'name': 'new-router',
                               'external_gateway_info': {'network_id':
                                                         ext_net_id}}}]
        self._test_check_update(entries)

        # clear the gw
        self._test_check_update(entries)
        router_q = {'name': 'new-router'}
        entries = [{'input': {'router_q': router_q, 'rtr_id': rtr_id},
                    'output': {'name': 'new-router',
                               'external_gateway_info': None}}]
        self._test_check_update(entries)

    def test_get(self):
        rtr_id = self._create_test_router('router-1')
        entries = [{'input': rtr_id,
                   'output': {'name': 'router-1',
                              'external_gateway_info': None,
                              'admin_state_up': True,
                              'status': 'ACTIVE',
                              'tenant_id':
                              self._uuid_to_str(self.proj_obj.uuid)}}]
        self._test_check_get(entries)

    def test_list(self):
        proj_1 = self._project_create('proj-1')
        proj_2 = self._project_create('proj-2')
        self._create_test_router('p1-router-1', tenant_id=proj_1.uuid)
        self._create_test_router('p1-router-2', tenant_id=proj_1.uuid)
        self._create_test_router('p2-router-1', tenant_id=proj_2.uuid)

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
             'output': [{'name': 'p1-router-1'},
                        {'name': 'p1-router-2'},
                        {'name': 'p2-router-1'}]},

            # non-admin context with proj-1 and with filters of proj-2
            {'input': {
                'context': {
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': False},
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_2.uuid)]}},
                'output': [{'name': 'p1-router-1'},
                           {'name': 'p1-router-2'}]},

            # admin context with proj-1 and with filters of proj-2
            {'input': {
                'context': {
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': True},
                'filters': {
                    'tenant_id': [self._uuid_to_str(proj_2.uuid)]}},
                'output': [{'name': 'p2-router-1'}]}]

        self._test_check_list(entries)

    def _create_test_subnet(self, name, net_obj, cidr='192.168.1.0/24'):
        subnet_q = {'name': name,
                    'cidr': cidr,
                    'ip_version': 4,
                    'network_id': str(net_obj.uuid)}
        ret = subnet_handler.SubnetHandler(
            self._test_vnc_lib).resource_create(subnet_q)
        subnet_uuid = ret['id']
        return subnet_uuid

    def _create_test_port(self, context, net_id, ip_address):
        port_q = {'network_id': net_id,
                  'tenant_id': context['tenant_id'],
                  'fixed_ips': [{'ip_address': ip_address}]}
        ret_port_q = vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_create(context, port_q)
        return ret_port_q['id']

    def _test_router_interface_helper(self):
        router_id = self._create_test_router('test-router')
        net_obj = vnc_api.VirtualNetwork('test-net', self.proj_obj)
        self._test_vnc_lib.virtual_network_create(net_obj)
        subnet_uuid = self._create_test_subnet('test-subnet', net_obj)
        return router_id, subnet_uuid, net_obj.uuid

    def test_add_remove_interface(self):
        router_id, subnet_id, _ = self._test_router_interface_helper()
        rtr_iface_handler = router_handler.LogicalRouterInterfaceHandler(
            self._test_vnc_lib)
        context = {'tenant': self._uuid_to_str(self.proj_obj.uuid),
                   'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                   'is_admin': False}

        self.assertRaises(bottle.HTTPError,
                          rtr_iface_handler.add_router_interface, context,
                          router_id)

        returned_output = rtr_iface_handler.add_router_interface(
            context, router_id, subnet_id=subnet_id)
        expected_output = {'id': router_id, 'subnet_id': subnet_id,
                           'tenant_id': context['tenant_id']}

        # get the port list and see if gw port is created or not
        port_list = vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_list(context)
        self.assertEqual(1, len(port_list))
        self.assertEqual('192.168.1.1',
                         port_list[0]['fixed_ips'][0]['ip_address'])
        expected_output['port_id'] = port_list[0]['id']
        self.assertEqual(expected_output, returned_output)

        # create a new router - router2 and attach the port_id of router-1
        # to this. It should raise HTTPError exception
        router_id_2 = self._create_test_router('test-router-2')
        self.assertRaises(bottle.HTTPError,
                          rtr_iface_handler.add_router_interface, context,
                          router_id_2, subnet_id=subnet_id)

        # test remove_router_interface
        returned_output = rtr_iface_handler.remove_router_interface(
            router_id, subnet_id=subnet_id)
        self.assertEqual(returned_output, expected_output)

        # get the port list and see if gw port is deleted or not
        port_list = vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_list(context)
        self.assertEqual(0, len(port_list))

        # removing again should raise HTTPError
        self.assertRaises(bottle.HTTPError,
                          rtr_iface_handler.remove_router_interface,
                          router_id, subnet_id=subnet_id)

    def test_add_remove_interface_port_id(self):
        context = {'tenant': self._uuid_to_str(self.proj_obj.uuid),
                   'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                   'is_admin': False}
        router_id, subnet_id, net_id = self._test_router_interface_helper()
        rtr_iface_handler = router_handler.LogicalRouterInterfaceHandler(
            self._test_vnc_lib)
        context = {'tenant': self._uuid_to_str(self.proj_obj.uuid),
                   'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                   'is_admin': False}
        port_id = self._create_test_port(context, net_id, '192.168.1.10')

        returned_output = rtr_iface_handler.add_router_interface(
            context, router_id, port_id=port_id)
        expected_output = {'id': router_id, 'subnet_id': subnet_id,
                           'tenant_id': context['tenant_id'],
                           'port_id': port_id}
        self.assertEqual(expected_output, returned_output)

        iface_port_info = vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_get(port_id)
        self.assertEqual('network:router_interface',
                         iface_port_info['device_owner'])
        self.assertEqual(router_id, iface_port_info['device_id'])

        # test remove_router_interface
        returned_output = rtr_iface_handler.remove_router_interface(
            router_id, port_id=port_id)
        self.assertEqual(returned_output, expected_output)

        # get the port list and see if port is deleted or not
        port_list = vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_list(context)
        self.assertEqual(0, len(port_list))
        # removing again should raise HTTPError
        # self.assertRaises(bottle.HTTPError,
        #                  rtr_iface_handler.remove_router_interface,
        #                  router_id, subnet_id=subnet_id)
