import bottle
from vnc_openstack import route_table_res_handler as route_table_handler
from vnc_openstack import subnet_res_handler as subnet_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vmi_res_handler as vmi_handler
from vnc_openstack import vn_res_handler as vn_handler


class TestVnHandlers(test_common.TestBase):
    def setUp(self):
        super(TestVnHandlers, self).setUp()
        self._handler = vn_handler.VNetworkHandler(self._test_vnc_lib)

    def test_create(self):
        self._test_failures_on_create(null_entry=True, invalid_tenant=True)

        entries = [{
            'input': {
                'network_q': {
                    'name': 'test-net-1',
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                }
            },
            'output': {
                'name': 'test-net-1',
                'router:external': False,
                'shared': False,
                'subnets': []
            }
        }, {
            'input': {
                'contrail_extensions_enabled': True,
                'network_q': {
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                    'router:external': True,
                    'shared': True
                }
            },
            'output': {
                'router:external': True,
                'shared': True,
                'contrail:fq_name': ['default-domain',
                                     'default-project',
                                     'default-virtual-network']
            }
        }]
        self._test_check_create(entries)

    def _create_network(self, name, proj, router_external=False, shared=False):
        res = vn_handler.VNetworkHandler(self._test_vnc_lib).resource_create(
            {'name': name,
             'tenant_id': self._uuid_to_str(proj.uuid),
             'router:external': router_external,
             'shared': shared
             })
        return res['id']

    def _create_test_subnet(self, name, net_obj, cidr='192.168.1.0/24'):
        subnet_q = {'name': name,
                    'cidr': cidr,
                    'ip_version': 4,
                    'network_id': str(net_obj.uuid)}
        ret = subnet_handler.SubnetHandler(
            self._test_vnc_lib).resource_create(subnet_q)
        subnet_uuid = ret['id']
        return subnet_uuid

    def test_update(self):
        self._test_failures_on_update()

        net_id = self._create_network(None, self.proj_obj)
        subnet_uuid = self._create_test_subnet(
            'test-subnet',
            self._test_vnc_lib.resources_collection['virtual_network'][net_id])
        route_table_handler.RouteTableHandler(
            self._test_vnc_lib).resource_create(
            {'name': 'test-route-table',
             'routes': [],
             'tenant_id': self._uuid_to_str(self.proj_obj.uuid)})

        # create a port ona different project
        proj_1 = self._project_create('proj-1')
        context = {'tenant_id': self._uuid_to_str(proj_1.uuid),
                   'is_admin': False}
        port_q = {'name': 'test-port',
                  'network_id': str(net_id),
                  'tenant_id': context['tenant_id']}
        vmi_handler.VMInterfaceHandler(
            self._test_vnc_lib).resource_create(
            context, port_q)

        entries = [{
            'input': {
                'net_id': net_id,
                'network_q': {
                    'name': 'test-net',
                    'admin_state_up': False,
                    'router:external': True,
                    'contrail:route_table': ['default-domain',
                                             'default-project',
                                             'test-route-table'],
                    'contrail:policys': [['default-domain',
                                          'default-project',
                                          'default-policy']]
                }
            },
            'output': {
                'name': 'test-net',
                'contrail:policys': [['default-domain',
                                      'default-project',
                                      'default-policy']],
                'admin_state_up': False,
                'id': net_id,
                'subnets': [subnet_uuid],
                'router:external': True,
                'contrail:route_table': [['default-domain',
                                          'default-project',
                                          'test-route-table']]
            }
        }, {
            # router:external flapping
            'input': {
                'net_id': net_id,
                'network_q': {
                    'router:external': False,
                }
            },
            'output': {
                'name': 'test-net',
                'id': net_id,
                'subnets': [subnet_uuid],
                'router:external': False,
            }
        }, {
            # invalid route_table entry
            'input': {
                'net_id': net_id,
                'network_q': {
                    'contrail:route_table': ['default-domain',
                                             'default-project',
                                             'test-rb']
                }
            },
            'output': bottle.HTTPError
        }, {
            # invalid use-case for shared attribute
            'input': {
                'net_id': net_id,
                'network_q': {
                    'shared': True
                }
            },
            'output': bottle.HTTPError
        }]

        self._test_check_update(entries)

    def test_delete(self):
        vn_uuid = self._create_network('test-net', self.proj_obj)

        entries = [{
            'input': {'net_id': vn_uuid},
            'output': None}]
        self._test_check_delete(entries)

    def test_list(self):
        proj_1 = self._project_create('proj-1')
        vn_normal = self._create_network('test-net', self.proj_obj)
        self._create_test_subnet(
            'test-subnet',
            self._test_vnc_lib.resources_collection[
                'virtual_network'][vn_normal])
        self._create_network('test-net-pr1', proj_1)
        self._create_network('test-re', proj_1, router_external=True)
        self._create_network('test-sh', proj_1, shared=True)

        context = {'tenant': self._uuid_to_str(proj_1.uuid),
                   'is_admin': False}
        entries = [{  # 0
            'input': {
                'context': context,
                'filters': {'name': 'test-net'}
            },
            'output': []
        }, {  # 1
            'input': {
                'context': context,
                'filters': {'name': 'test-re'}
            },
            'output': [{'name': 'test-re'}]
        }, {  # 2
            'input': {
                'context': context,
                'filters': {'router:external': [True]}
            },
            'output': [{'name': 'test-re'}]
        }, {  # 3
            'input': {
                'context': context,
                'filters': {'name': 'test-re',
                            'router:external': [True],
                            'shared': [True]}
            },
            'output': []
        }, {  # 4
            'input': {
                'context': context,
                'filters': {'shared': [True]}
            },
            'output': [{'name': 'test-sh'}]
        }, {  # 5
            'input': {
                'context': context,
                'filters': {'shared': [True], 'router:external': [True]}
            },
            'output': []
        }, {  # 6
            'input': {
                'context': context,
                'filters': {'id': [vn_normal]}
            },
            'output': [{'name': 'test-net'}]
        }, {  # 7
            'input': {
                'context': context,
                'filters': {}
            },
            'output': [{'name': 'test-re'},
                       {'name': 'test-sh'},
                       {'name': 'test-net-pr1'}]
        }, {  # 8
            'input': {
                'context': context,
                'filters': {'tenant_id': [self._uuid_to_str(proj_1.uuid)],
                            'router:external': [True]}
            },
            'output': [{'name': 'test-re'}]
        }]
        self._test_check_list(entries)

        context['is_admin'] = True
        entries[0]['output'] += [{'name': 'test-net'}]
        entries[5]['output'] += [{'name': 'test-sh'}]
        entries[7]['output'] += [{'name': 'test-net'}]
        entries[8]['output'].extend(
            [{'name': 'test-sh'}, {'name': 'test-net-pr1'}])
        self._test_check_list(entries)

        res = vn_handler.VNetworkHandler(self._test_vnc_lib).vn_list_shared()
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0].name, 'test-sh')

    def test_get(self):
        self._test_failures_on_get()

        vn_uuid = self._create_network('test-net', self.proj_obj)

        entries = [{
            'input': vn_uuid,
            'output': {'name': 'test-net'}}]
        self._test_check_get(entries)

    def test_count(self):
        proj_1 = self._project_create('proj-1')
        self._create_network('test-net', self.proj_obj)
        self._create_network('test-re', proj_1, router_external=True)
        self._create_network('test-sh', proj_1, shared=True)

        entries = [{
            'input': {
                'filters': {}
            },
            'output': 3
        }, {
            'input': {
                'filters': {'router:external': [True]}
            },
            'output': 1}]
        self._test_check_count(entries)
