from vnc_api import vnc_api
from vnc_openstack import route_table_res_handler as route_table_handler
from vnc_openstack import svc_instance_res_handler as svc_instance_handler
from vnc_openstack.tests import test_common
from vnc_openstack import vn_res_handler as vn_handler


class TestRouteTableHandlers(test_common.TestBase):
    def setUp(self):
        super(TestRouteTableHandlers, self).setUp()
        self._svc_handler = svc_instance_handler.SvcInstanceHandler(
            self._test_vnc_lib)
        self._handler = route_table_handler.RouteTableHandler(
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

    def _create_test_svc_instance(self, name, proj):
        ext_net = self._create_external_network()

        si_q = {
            'name': 'test-svc',
            'tenant_id': proj.uuid,
            'external_net': ext_net
        }

        res = self._svc_handler.resource_create(si_q)
        return res['uuid']

    def test_create(self):
        svc_uuid = self._create_test_svc_instance('test-svc', self.proj_obj)
        svc_obj = self._test_vnc_lib.resources_collection['service_instance'][
            svc_uuid]
        vm_obj = vnc_api.VirtualMachine('test-vm')
        vm_obj.set_service_instance(svc_obj)
        vm_uuid = self._test_vnc_lib.virtual_machine_create(vm_obj)

        entries = [{
            'input': {
                'rt_q': {
                    'tenant_id': self.proj_obj.uuid,
                    'name': 'test-rt',
                    'routes': {
                        'route': [{
                            'next_hop': test_common.INVALID_UUID
                        }]
                    }
                }
            },
            'output': {
                'fq_name': ['default-domain', 'default-project', 'test-rt'],
                'routes': None,
                'name': 'test-rt'
            }
        }, {
            'input': {
                'rt_q': {
                    'tenant_id': self.proj_obj.uuid,
                    'name': 'test-rt-1',
                    'routes': {
                        'route': [{
                            'next_hop': vm_uuid
                        }]
                    }
                }
            },
            'output': {
                'fq_name': ['default-domain', 'default-project', 'test-rt-1'],
                'routes': {
                    'route': [{
                        'prefix': None,
                        'next_hop': 'default-domain:default-project:test-svc',
                        'next_hop_type': None
                    }]
                },
                'name': 'test-rt-1'
            }
        }]
        self._test_check_create(entries)

    def _create_test_route_table(self, name, proj_obj):
        rt_q = {
            'tenant_id': proj_obj.uuid,
            'name': name,
            'routes': {}
        }
        r = self._handler.resource_create(rt_q)
        return r['uuid']

    def test_delete(self):
        self._test_failures_on_delete()

        rt_uuid = self._create_test_route_table('test-rt', self.proj_obj)

        self.assertTrue(rt_uuid in self._test_vnc_lib.resources_collection[
            'route_table'])

        self._handler.resource_delete(rt_uuid)

        self.assertFalse(rt_uuid in self._test_vnc_lib.resources_collection[
            'route_table'])

    def test_update(self):
        self._test_failures_on_update()
        svc_uuid = self._create_test_svc_instance('test-svc', self.proj_obj)
        svc_obj = self._test_vnc_lib.resources_collection['service_instance'][
            svc_uuid]
        vm_obj = vnc_api.VirtualMachine('test-vm')
        vm_obj.set_service_instance(svc_obj)
        vm_uuid = self._test_vnc_lib.virtual_machine_create(vm_obj)

        rt_uuid = self._create_test_route_table('test-rt', self.proj_obj)

        entries = [{
            'input': rt_uuid,
            'output': {
                'routes': None
            }
        }]
        self._test_check_get(entries)

        entries = [{
            'input': {
                'rt_id': rt_uuid,
                'rt_q': {
                    'routes': {
                        'route': [{
                            'next_hop': test_common.INVALID_UUID
                        }]
                    }
                }
            },
            'output': {
                'routes': None
            }
        }, {
            'input': {
                'rt_id': rt_uuid,
                'rt_q': {
                    'routes': {
                        'route': [{
                            'next_hop': vm_uuid
                        }]
                    }
                }
            },
            'output': {
                'routes': {
                    'route': [{
                        'prefix': None,
                        'next_hop': 'default-domain:default-project:test-svc',
                        'next_hop_type': None
                    }]
                }
            }
        }]
        self._test_check_update(entries)

    def test_list(self):
        proj_1 = self._project_create('proj-1')
        rt_1_uuid = self._create_test_route_table('test-rt-1', proj_1)
        rt_2_uuid = self._create_test_route_table('test-rt-2', self.proj_obj)

        entries = [{
            'input': {
                'context': {
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': False
                },
                'filters': {
                    'tenant_id': [self.proj_obj.uuid]
                }
            },
            'output': [{
                'name': 'test-rt-2'
            }]
        }, {
            'input': {
                'context': {
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': False
                },
                'filters': {
                    'name': ['test-rt-2']
                }
            },
            'output': [{
                'name': 'test-rt-2'
            }]
        }, {
            'input': {
                'context': {
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': True
                },
                'filters': {
                    'id': rt_2_uuid
                }
            },
            'output': [{
                'uuid': rt_2_uuid
            }]
        }, {
            'input': {
                'context': {
                    'tenant': self._uuid_to_str(proj_1.uuid),
                    'is_admin': True
                },
                'filters': {}
            },
            'output': [{
                'uuid': rt_1_uuid
            }, {
                'uuid': rt_2_uuid
            }]
        }]
        self._test_check_list(entries)

    def test_check_get(self):
        self._test_failures_on_get()
