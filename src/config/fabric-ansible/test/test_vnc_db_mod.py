import unittest
import sys
from flexmock import flexmock
from test_fabric_base import TestFabricModule
from test_fabric_base import set_module_args
from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import *

sys.path.append('../fabric-ansible/ansible-playbooks/module_utils')
import fabric_utils
from job_manager.job_utils import JobVncApi


class TestFabricVncDbModule(TestFabricModule):

    def setUp(self):
        fake_logger = flexmock()
        flexmock(fake_logger).should_receive('error')
        flexmock(fake_logger).should_receive('debug')
        flexmock(fabric_utils).should_receive('fabric_ansible_logger').and_return(fake_logger)

    def tearDown(self):
        pass

    def _test_invalid_vnc_op(self):
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='get', object_dict={"uuid": "1234"},
                 job_ctx={"auth_token": "1234"}))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def _test_object_type_not_present(self):
        set_module_args(dict(enable_job_ctx=False, object_op='get',
                             object_dict={"uuid": "1234"},
                             job_ctx={"auth_token": "1234"}))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    # test when required param auth_token not present in job_ctx
    def _test_auth_token_not_present(self):
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='get',
                 object_dict={"uuid": "1234"}, job_ctx={}))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    # test when job_ctx is not present
    def _test_job_ctx_not_present(self):
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='get', object_dict={"uuid": "1234"}))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    # test when enable_job_ctx is True by default and some other
    # required params in job_ctx like job template fqname, etc are missing
    def _test_job_ctx_reqd_params_not_present(self):
        set_module_args(dict(object_type='physical_router', object_op='get',
                             object_dict={"uuid": "1234"},
                             job_ctx={"auth_token": "1234"}))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def _test_invalid_vnc_class(self):
        set_module_args(dict(object_type='lldp', object_op='get',
                             object_dict={"uuid": "1234"}, auth_token="1234"))
        result = self.execute_module(failed=True)
        self.assertEqual(result['failed'], True)

    def _test_vnc_read(self):
        fake_vnc_lib = flexmock()
        flexmock(JobVncApi).should_receive('vnc_init').and_return(fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router = PhysicalRouter(name='test-tenant')
        fake_router_obj = {"physical-router": {"name": "Router-1"}}
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_read').and_return(fake_router)
        flexmock(fake_vnc_lib).should_receive(
            'obj_to_dict').and_return(fake_router_obj)
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='read', object_dict={"uuid": "1234"},
                 job_ctx={"auth_token": "1234"}))
        result = self.execute_module()
        self.assertEqual(result.get('failed'), None)

    def _test_vnc_create(self):
        object_dict = {"parent_type": "global-system-config",
                       "fq_name": ["default-global-system-config", "mx-240"],
                       "physical_router_management_ip": "172.10.68.1",
                       "physical_router_vendor_name": "Juni",
                       "physical_router_product_name": "MX",
                       "physical_router_device_family": "junos"}
        fake_vnc_lib = flexmock()
        flexmock(JobVncApi).should_receive('vnc_init').and_return(fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router_uuid = "1ef6cf9d-c2e2-4004-810a-43d471c94dc5"
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_create').and_return(fake_router_uuid)
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='create', object_dict=object_dict,
                 job_ctx={"auth_token": "1234"}))
        result = self.execute_module()
        self.assertEqual(result.get('failed'), None)

    def _test_vnc_update(self):
        object_dict = {
            "uuid": "1ef6cf9d-c2e2-4004-810a-43d471c94dc5",
            "physical_router_user_credentials": {
                "username": "root",
                "password": "*****"
            }
        }
        fake_vnc_lib = flexmock()
        flexmock(JobVncApi).should_receive('vnc_init').and_return(fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router_obj = "{'physical-router' : {'uuid': '1ef6cf9d-c2e2-4004-810a-43d471c94dc5'}}"
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_update').and_return(fake_router_obj)
        flexmock(fake_vnc_lib).should_receive(
            'id_to_fq_name').and_return('MX-240')
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='update', object_dict=object_dict,
                 job_ctx={"auth_token": "1234"}))
        result = self.execute_module()
        self.assertEqual(result.get('failed'), None)

    def _test_vnc_read_with_job_params(self):
        fake_vnc_lib = flexmock()
        flexmock(JobVncApi).should_receive('vnc_init').and_return(fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router = PhysicalRouter(name='test-tenant')
        fake_router_obj = {"physical-router": {"name": "Router-1"}}
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_read').and_return(fake_router)
        flexmock(fake_vnc_lib).should_receive(
            'obj_to_dict').and_return(fake_router_obj)
        set_module_args(
            dict(object_type='physical_router',
                 object_op='read', object_dict={"uuid": "1234"},
                 job_ctx={"auth_token": "1234",
                          "job_template_fqname": [
                              "default-global-system-config",
                              "device_import_template"],
                          "job_execution_id": "123",
                          "config_args": "546", "job_input": ""}))
        result = self.execute_module()
        self.assertEqual(result.get('failed'), None)

    def _test_vnc_bulk_create(self):
        object_list = [{"parent_type": "global-system-config",
                        "fq_name": ["default-global-system-config", "mx-240"],
                        "physical_router_management_ip": "172.10.68.1",
                        "physical_router_vendor_name": "Juniper",
                        "physical_router_product_name": "MX",
                        "physical_router_device_family": "junos"}]
        fake_vnc_lib = flexmock()
        flexmock(JobVncApi).should_receive('vnc_init').and_return(fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router_uuid = "1ef6cf9d-c2e2-4004-810a-43d471c94dc5"
        fake_router = PhysicalRouter(name='mx-240')
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_read').and_return(fake_router)
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_create').and_return(fake_router_uuid)
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='bulk_create', object_list=object_list,
                 job_ctx={"auth_token": "1234"}))
        result = self.execute_module()
        self.assertEqual(result.get('failed'), None)

    def _test_vnc_bulk_update(self):
        object_list = [{
            "uuid": "1ef6cf9d-c2e2-4004-810a-43d471c94dc5",
            "physical_router_user_credentials": {
                "username": "root",
                "password": "*****"
            }
        }]
        fake_vnc_lib = flexmock()
        flexmock(JobVncApi).should_receive('vnc_init').and_return(fake_vnc_lib)
        fake_vnc_lib.should_receive('__init__')
        fake_router_obj = "{'physical-router' : {'uuid': '1ef6cf9d-c2e2-4004-810a-43d471c94dc5'}}"
        flexmock(fake_vnc_lib).should_receive(
            'physical_router_update').and_return(fake_router_obj)
        flexmock(fake_vnc_lib).should_receive(
            'id_to_fq_name').and_return('MX-240')
        set_module_args(
            dict(enable_job_ctx=False, object_type='physical_router',
                 object_op='bulk_update', object_list=object_list,
                 job_ctx={"auth_token": "1234"}))
        result = self.execute_module()
        self.assertEqual(result.get('failed'), None)

