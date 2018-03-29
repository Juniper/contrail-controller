import unittest
from flexmock import flexmock
from ansible.modules.network.fabric import vnc_db_mod
from test_fabric_base import TestFabricModule
from test_fabric_base import set_module_args
from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import *


class TestFabricVncDbModule(TestFabricModule):

    module = vnc_db_mod

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_invalid_vnc_op(self):
        set_module_args(dict(object_type='physical_router', object_op='get',
                             object_dict={"uuid": "1234"}, auth_token="1234"))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def test_object_type_not_present(self):
        set_module_args(dict(object_op='get',
                             object_dict={"uuid": "1234"}, auth_token="1234"))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def test_auth_token_not_present(self):
        set_module_args(dict(object_type='physical_router', object_op='get',
                             object_dict={"uuid": "1234"}))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def test_invalid_vnc_class(self):
        set_module_args(dict(object_type='lldp', object_op='get',
                             object_dict={"uuid": "1234"}, auth_token="1234"))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def test_vnc_read(self):
        fake_vnc_lib = flexmock()
        flexmock(VncApi, __new__=fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router = PhysicalRouter(name='test-tenant')
        fake_router_obj = {"physical-router": {"name": "Router-1"}}
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_read').and_return(fake_router)
        flexmock(fake_vnc_lib).should_receive(
            'obj_to_dict').and_return(fake_router_obj)
        set_module_args(dict(object_type='physical_router', object_op='read',
                             object_dict={"uuid": "1234"}, auth_token="1234"))
        result = self.execute_module()
        self.assertEqual(result['failed'], False)

    def test_vnc_create(self):
        object_dict = {"parent_type": "global-system-config",
                       "fq_name": ["default-global-system-config", "mx-240"],
                       "physical_router_management_ip": "172.10.68.1",
                       "physical_router_vendor_name": "Juni",
                       "physical_router_product_name": "MX",
                       "physical_router_device_family": "juniper-mx"}
        fake_vnc_lib = flexmock()
        flexmock(VncApi, __new__=fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router_uuid = "1ef6cf9d-c2e2-4004-810a-43d471c94dc5"
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_create').and_return(fake_router_uuid)
        set_module_args(dict(object_type='physical_router', object_op='create',
                             object_dict=object_dict, auth_token="1234"))
        result = self.execute_module()
        self.assertEqual(result['failed'], False)

    def test_vnc_update(self):
        object_dict = {
            "uuid": "1ef6cf9d-c2e2-4004-810a-43d471c94dc5",
            "physical_router_user_credentials": {
                "username": "root",
                "password": "*****"
            }
        }
        fake_vnc_lib = flexmock()
        flexmock(VncApi, __new__=fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router_obj = "{'physical-router' : {'uuid': '1ef6cf9d-c2e2-4004-810a-43d471c94dc5'}}"
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_update').and_return(fake_router_obj)
        flexmock(fake_vnc_lib).should_receive(
            'id_to_fq_name').and_return('MX-240')
        set_module_args(dict(object_type='physical_router', object_op='update',
                             object_dict=object_dict, auth_token="1234"))
        result = self.execute_module()
        self.assertEqual(result['failed'], False)
