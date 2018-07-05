#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.monkey

gevent.monkey.patch_all()
import sys
import os
import logging
import json
sys.path.append("../test/utils")
from util import retry
import test_case
import shutil


logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


# Positive test case - using the actual json & schema files
# Tags and Job templates should be created
# class TestInitData1(test_case.ApiServerTestCase):
#     @classmethod
#     def setUpClass(cls, *args, **kwargs):
#
#         cls.console_handler = logging.StreamHandler()
#         cls.console_handler.setLevel(logging.DEBUG)
#         logger.addHandler(cls.console_handler)
#         super(TestInitData1, cls).setUpClass(
#             extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
#                                  "../fabric-ansible/ansible-playbooks/")])
#
#     # end setUpClass
#
#     @classmethod
#     def tearDownClass(cls, *args, **kwargs):
#         logger.removeHandler(cls.console_handler)
#         super(TestInitData1, cls).tearDownClass(*args, **kwargs)
#
#     @retry(tries=5, delay=5)
#     def test_load_init_data1(self):
#         result = True
#         expected_jb_count = 0
#         expected_tag_count = 0
#         exepected_object_type_count = 0
#
#         try:
#         # read the conf file to get the actual count
#             with open("../fabric-ansible/ansible-playbooks/conf/predef_payloads.json", "r") as f:
#                 json_data = json.load(f)
#
#             if json_data:
#                 data = json_data.get("data")
#                 expected_object_type_count = len(data)
#                 for d in data:
#                     if d.get("object_type") == 'job_template':
#                         expected_jb_count = len(d.get("objects"))
#                     if d.get("object_type") == 'tag':
#                         expected_tag_count = len(d.get("objects"))
#
#             # self.assertEqual(exepected_object_type_count, 3)
#             ipam_fq_name = ['default-domain', 'default-project',
#                             'default-network-ipam']
#             ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
#
#             if (ipam_obj.get_perms2().global_access != 5):
#                     result = False
#             jb_list = self._vnc_lib.job_templates_list()
#             self.assertTrue(jb_list)
#             self.assertEqual(len(jb_list.get('job-templates')), expected_jb_count)
#             tags = self._vnc_lib.tags_list()
#             self.assertTrue(tags)
#             self.assertEqual(len(tags.get('tags')), expected_tag_count)
#         except:
#             self.logger.error("Exception Verifying Job Templates")
#             result = False
#         return result


# Testing when schema file is empty for the job template, tags and
# job templates should still be created
class TestInitData2(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = {"data": [{"object_type": "job_template",
                               "objects": [
                                   {"fq_name": ["default-global-system-config",
                                                "image_upgrade_template"],
                                    "job_template_multi_device_job": "true",
                                    "job_template_playbooks": {
                                        "playbook_info": [{"device_family": "",
                                                           "vendor": "Juniper",
                                                           "playbook_uri": "./image_upgrade.yml"}]},
                                    "job_template_input_schema": "",
                                    "job_template_output_schema": "",
                                    "parent_type": "global-system-config"}]},
                              {"object_type": "tag",
                               "objects": [
                                   {"fq_name": ["label=fabric-management_ip"],
                                    "name": "label=fabric-management_ip",
                                    "tag_type_name": "label",
                                    "tag_value": "fabric-management_ip"}]}]}
        schema_data = {}
        if not os.path.exists("conf"):
            os.makedirs("conf")
        if not os.path.exists("schema"):
            os.makedirs("schema")
        with open("conf/predef_payloads.json", "a+") as f:
            json.dump(json_data, f)
        with open("schema/image_upgrade_schema.json", "a+") as file:
            json.dump(schema_data, file)
        super(TestInitData2, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 ".")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        if os.path.exists("conf"):
            shutil.rmtree("conf")
        if os.path.exists("schema"):
            shutil.rmtree("schema")
        super(TestInitData2, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass
    @retry(tries=5, delay=5)
    def test_load_init_data_2(self):
        result = True
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                        'default-network-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj.get_perms2().global_access != 5):
                result = False
            jb_list = self._vnc_lib.job_templates_list()
            self.assertTrue(len(jb_list.get('job-templates')) > 0)
            tags = self._vnc_lib.tags_list()
            self.assertTrue(len(tags.get('tags')) > 0)
        except:
            self.logger.error("Exception Verifying Job Templates")
            result = False
        return result


# Test when no schema files are present.
class TestInitDataError1(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)

        data = {"data": [{"object_type": "job_template",
                          "objects": [
                              {"fq_name": ["default-global-system-config",
                                           "image_upgrade_template"],
                               "job_template_multi_device_job": "true",
                               "job_template_playbooks": {
                                   "playbook_info": [{"device_family": "",
                                                      "vendor": "Juniper",
                                                      "playbook_uri": "./image_upgrade.yml"}]},
                               "job_template_input_schema": "",
                               "job_template_output_schema": "",
                               "parent_type": "global-system-config"}]},
                         {"object_type": "tag",
                          "objects": [
                              {"fq_name": ["label=fabric-management_ip"],
                               "name": "label=fabric-management_ip",
                               "tag_type_name": "label",
                               "tag_value": "fabric-management_ip"}]}]}
        if not os.path.exists("conf"):
            os.makedirs("conf")
        if not os.path.exists("schema"):
            os.makedirs("schema")
        with open("conf/predef_payloads.json", "a+") as f:
            json.dump(data, f)
        super(TestInitDataError1, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 ".")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        if os.path.exists("conf"):
            shutil.rmtree("conf")
        if os.path.exists("schema"):
            shutil.rmtree("schema")
        super(TestInitDataError1, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass

    @retry(tries=5, delay=5)
    def test_load_init_data_01(self):
        result = True
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'default-network-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj.get_perms2().global_access != 5):
                result = False
            jb_list = self._vnc_lib.job_templates_list()
            self.assertEquals(len(jb_list.get('job-templates')), 0)
            tags = self._vnc_lib.tags_list()
            self.assertEquals(len(tags.get('tags')), 0)
        except:
            self.logger.error("Exception Verifying Job Templates")
            result = False
        return result


# Test when object_type having invalid name
class TestInitDataError2(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = {
            "data": [
                {
                    "object_type": "abc",
                    "objects": [{"fq_name": ["test"]}]
                },
                {
                    "object_type": "tag",
                    "objects": [
                        {
                            "fq_name": [
                                "label=fabric-management_ip"
                            ],
                            "name": "label=fabric-management_ip",
                            "tag_type_name": "label",
                            "tag_value": "fabric-management_ip"
                        }
                    ]
                }
            ]
        }
        if not os.path.exists("conf"):
            os.makedirs("conf")
        with open("conf/predef_payloads.json", "a+") as f:
            json.dump(json_data, f)
        super(TestInitDataError2, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 ".")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        if os.path.exists("conf"):
            shutil.rmtree("conf")
        super(TestInitDataError2, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass
    @retry(tries=5, delay=5)
    def test_load_init_data_02(self):
        result = True
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'default-network-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj.get_perms2().global_access != 5):
                result = False
            jb_list = self._vnc_lib.job_templates_list()
            self.assertEquals(len(jb_list.get('job-templates')), 0)
            tags = self._vnc_lib.tags_list()
            self.assertEquals(len(tags.get('tags')), 0)
        except:
            self.logger.error("Exception Verifying Job Templates")
            result = False
        return result


# Testing when schema directory is not present.
class TestInitDataError3(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = {"data": [{"object_type": "job_template",
                               "objects": [
                                   {"fq_name": ["default-global-system-config",
                                                "image_upgrade_template"],
                                    "job_template_multi_device_job": "true",
                                    "job_template_playbooks": {
                                        "playbook_info": [{"device_family": "",
                                                           "vendor": "Juniper",
                                                           "playbook_uri": "./image_upgrade.yml"}]},
                                    "job_template_input_schema": "",
                                    "job_template_output_schema": "",
                                    "parent_type": "global-system-config"}]},
                              {"object_type": "tag",
                               "objects": [
                                   {"fq_name": ["label=fabric-management_ip"],
                                    "name": "label=fabric-management_ip",
                                    "tag_type_name": "label",
                                    "tag_value": "fabric-management_ip"}]}]}

        if not os.path.exists("conf"):
            os.makedirs("conf")
        with open("conf/predef_payloads.json", "a+") as f:
            json.dump(json_data, f)
        super(TestInitDataError3, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 ".")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        if os.path.exists("conf"):
            shutil.rmtree("conf")
        super(TestInitDataError3, cls).tearDownClass(*args, **kwargs)
        # end tearDownClass

    @retry(tries=5, delay=5)
    def test_load_init_data_03(self):
        result = True
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'default-network-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj.get_perms2().global_access != 5):
                result = False
            jb_list = self._vnc_lib.job_templates_list()
            self.assertEquals(len(jb_list.get('job-templates')), 0)
            tags = self._vnc_lib.tags_list()
            self.assertEquals(len(tags.get('tags')), 0)
        except:
            self.logger.error("Exception Verifying Job Templates")
            result = False
        return result


# Testing when json is invalid
class TestInitDataError4(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = "abc"
        if not os.path.exists("conf"):
            os.makedirs("conf")
        with open("conf/predef_payloads.json", "a+") as f:
            f.write(json_data)
        super(TestInitDataError4, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 ".")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        if os.path.exists("conf"):
            shutil.rmtree("conf")
        super(TestInitDataError4, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass
    @retry(tries=5, delay=5)
    def test_load_init_data_04(self):
        result = True
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'default-network-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj.get_perms2().global_access != 5):
                result = False
            jb_list = self._vnc_lib.job_templates_list()
            self.assertEquals(len(jb_list.get('job-templates')), 0)
            tags = self._vnc_lib.tags_list()
            self.assertEquals(len(tags.get('tags')), 0)
        except:
            self.logger.error("Exception Verifying Job Templates")
            result = False
        return result

# Testing when tag type is unknown
class TestInitDataError5(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        # create a file in current dir and put some invalid json
        # create predef_payloads.json and schema/files
        json_data = {
            "data": [
                {
                    "object_type": "tag",
                    "objects": [
                        {
                            "fq_name": [
                                "abc=management_ip"
                            ],
                            "name": "abc=management_ip",
                            "tag_type_name": "abc",
                            "tag_value": "management_ip"
                        }
                    ]
                }
            ]
        }

        if not os.path.exists("conf"):
            os.makedirs("conf")
        with open("conf/predef_payloads.json", "a+") as f:
            json.dump(json_data, f)
        super(TestInitDataError5, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 ".")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        if os.path.exists("conf"):
            shutil.rmtree("conf")
        super(TestInitDataError5, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass

    @retry(tries=5, delay=5)
    def test_load_init_data_05(self):
        result = True
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'default-network-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj.get_perms2().global_access != 5):
                result = False
            jb_list = self._vnc_lib.job_templates_list()
            self.assertEquals(len(jb_list.get('job-templates')), 0)
            tags = self._vnc_lib.tags_list()
            self.assertEquals(len(tags.get('tags')), 0)
        except:
            self.logger.error("Exception Verifying Job Templates")
            result = False
        return result
