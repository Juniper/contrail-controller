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
from vnc_api.vnc_api import *
from vnc_api.gen.resource_test import *
sys.path.append('../common/tests')
from test_utils import *
import test_case
import shutil
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

# Positive test case - using the actual json & schema files, tags and templates should be created 
class TestInitData1(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestInitData1, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                "../fabric-ansible/ansible-playbooks/")])
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestInitData1, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_load_init_data(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertTrue(jb_list)
        self.assertTrue(len(jb_list.get('job-templates')) == 4)
        tags = self._vnc_lib.tags_list()
        self.assertTrue(tags)
        self.assertTrue(len(tags.get('tags')) == 2)

# Testing when schema file is empty for the job template, tags and job templates should still be created
class TestInitData2(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = {
            "data": [
                {
                    "object_type": "job_template",
                    "objects": [
                        {
                            "fq_name": [
                                "default-global-system-config",
                                "image_upgrade_template"
                            ],
                            "job_template_multi_device_job": "true",
                            "job_template_playbooks": {
                                "playbook_info": [
                                    {
                                        "device_family": "",
                                        "vendor": "Juniper",
                                        "playbook_uri": "./opt/contrail/fabric_ansible_playbooks/image_upgrade.yml"
                                    }
                                ]
                            },
                            "job_template_input_schema": "",
                            "job_template_output_schema": "",
                            "parent_type": "global-system-config"
                        }
                    ]
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

    def test_load_init_data_02(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertTrue(len(jb_list.get('job-templates')) > 0)
        tags = self._vnc_lib.tags_list()
        self.assertTrue(len(tags.get('tags')) > 0)

#Test when no schema files are present.
class TestInitDataError1(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)

        data = {
            "data": [
                {
                    "object_type": "job_template",
                    "objects": [
                        {
                            "fq_name": [
                                "default-global-system-config",
                                "image_upgrade_template"
                            ],
                            "job_template_multi_device_job": "true",
                            "job_template_playbooks": {
                                "playbook_info": [
                                    {
                                        "device_family": "",
                                        "vendor": "Juniper",
                                        "playbook_uri": "./opt/contrail/fabric_ansible_playbooks/image_upgrade.yml"
                                    }
                                ]
                            },
                            "job_template_input_schema": "",
                            "job_template_output_schema": "",
                            "parent_type": "global-system-config"
                        }
                    ]
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
        if not os.path.exists("schema"):
            os.makedirs("schema")
        with open("conf/predef_payloads.json", "a+") as f:
            json.dump(data,f)
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

    def test_load_init_data_01(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertEquals(len(jb_list.get('job-templates')), 0)
        tags = self._vnc_lib.tags_list()
        self.assertEquals(len(tags.get('tags')),  0)

#Test when object_type having invalid name
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
        if os.path.exists("schema"):
            shutil.rmtree("schema")
        super(TestInitDataError2, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_load_init_data_02(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertEquals(len(jb_list.get('job-templates')), 0)
        tags = self._vnc_lib.tags_list()
        self.assertEquals(len(tags.get('tags')), 0)

# Testing when schema directory is not present.
class TestInitDataError3(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = {
            "data": [
                {
                    "object_type": "job_template",
                    "objects": [
                        {
                            "fq_name": [
                                "default-global-system-config",
                                "image_upgrade_template"
                            ],
                            "job_template_multi_device_job": "true",
                            "job_template_playbooks": {
                                "playbook_info": [
                                    {
                                        "device_family": "",
                                        "vendor": "Juniper",
                                        "playbook_uri": "./opt/contrail/fabric_ansible_playbooks/image_upgrade.yml"
                                    }
                                ]
                            },
                            "job_template_input_schema": "",
                            "job_template_output_schema": "",
                            "parent_type": "global-system-config"
                        }
                    ]
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
        super(TestInitDataError3, cls).setUpClass(
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
        super(TestInitDataError3, cls).tearDownClass(*args, **kwargs)
        # end tearDownClass

    def test_load_init_data_03(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertEquals(len(jb_list.get('job-templates')), 0)
        tags = self._vnc_lib.tags_list()
        self.assertEquals(len(tags.get('tags')), 0)

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
        if os.path.exists("schema"):
            shutil.rmtree("schema")
        super(TestInitDataError4, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_load_init_data_04(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertEquals(len(jb_list.get('job-templates')), 0)
        tags = self._vnc_lib.tags_list()
        self.assertEquals(len(tags.get('tags')), 0)

#Testing when tag type is unknown
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

    def test_load_init_data_06(self):
        jb_list = self._vnc_lib.job_templates_list()
        self.assertEquals(len(jb_list.get('job-templates')), 0)
        tags = self._vnc_lib.tags_list()
        self.assertEquals(len(tags.get('tags')), 0)

