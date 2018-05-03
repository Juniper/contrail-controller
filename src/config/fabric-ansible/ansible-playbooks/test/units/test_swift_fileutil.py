import unittest
from flexmock import flexmock
import swiftclient
import swiftclient.utils
from ansible.modules.network.fabric import swift_fileutil
from test_fabric_base import TestFabricModule
from test_fabric_base import set_module_args
from ansible.module_utils import fabric_utils


class TestSwiftFileUtilModule(TestFabricModule):
    module = swift_fileutil

    def setUp(self):
        super(TestSwiftFileUtilModule, self).setUp()
        # Mocking the swift connection object
        self.mockobj = flexmock().should_receive('get_account').and_return(['storageurl']).mock()
        flexmock(swiftclient.client).should_receive('Connection').and_return(self.mockobj)
        flexmock(self.mockobj).should_receive('post_account').and_return(None)
        flexmock(self.mockobj).url = "storage_url"
        flexmock(self.mockobj).should_receive("close").and_return(None)
        fake_logger = flexmock()
        flexmock(fake_logger).should_receive('error')
        flexmock(fake_logger).should_receive('debug')
        flexmock(fabric_utils).should_receive('fabric_ansible_logger').and_return(fake_logger)

        self.args_dict = dict(authtoken="4242", authurl="auth_url", user="admin", key="contrail", tenant_name="project",
                              auth_version="3.0", temp_url_key="temp_url_key1",
                              temp_url_key_2="temp_url_key2", chosen_temp_url_key="temp_url_key",
                              container_name="container", filename="sample.txt", expirytime=3600)

    # Testing the swift utility module
    def test_fileutility01(self):
        fake_image_url = "/v1/sample.txt"
        flexmock(swiftclient.utils).should_receive('generate_temp_url').and_return(fake_image_url)
        set_module_args(self.args_dict)
        result = self.execute_module()
        self.assertTrue(result["url"])
        self.assertEqual(result["url"], fake_image_url)

    # Testing when generate_temp_url returns None
    def test_fileutility02(self):
        flexmock(swiftclient.utils).should_receive('generate_temp_url').and_return(None)
        set_module_args(self.args_dict)
        self.assertRaises(Exception, self.execute_module())

    # Testing when generate_temp_url raises exception
    def test_fileutility_03(self):
        flexmock(swiftclient.utils).should_receive('generate_temp_url').and_raise(Exception)
        set_module_args(self.args_dict)
        self.assertRaises(Exception, self.execute_module())

    # #Testing the swift connection after retry
    def test_fileutility04(self):
        flexmock(swiftclient.client).should_receive('Connection').and_return(None)
        self.args_dict['connection_retry_count'] = 1
        set_module_args(self.args_dict)
        self.assertRaises(Exception, self.execute_module())

    # Testing the update account error
    def test_fileutility05(self):
        flexmock(self.mockobj).should_receive('post_account').and_raise(Exception)
        set_module_args(self.args_dict)
        self.assertRaises(Exception, self.execute_module())

    # Testing the case where optional args are not passed and it should take default value
    def test_fileutility06(self):
        for e in ["tenant_name","auth_version","chosen_temp_url_key","connection_retry_count"]:
            self.args_dict.pop(e, None)
        set_module_args(self.args_dict)
        fake_image_url = "/v1/sample.txt"
        flexmock(swiftclient.utils).should_receive('generate_temp_url').and_return(fake_image_url)
        result = self.execute_module()
        self.assertTrue(result["url"])
        self.assertEqual(result["url"], fake_image_url)

