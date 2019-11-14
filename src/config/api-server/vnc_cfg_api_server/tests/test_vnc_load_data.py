from __future__ import print_function
from __future__ import absolute_import
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from builtins import str
from builtins import range
import sys
import os
import logging
import json
from . import test_case
from vnc_api.exceptions import NoIdError, RefsExistError
from vnc_api.gen.resource_client import *
from vnc_api.gen.resource_xsd import *
from vnc_api.utils import obj_type_to_vnc_class
import shutil

from time import sleep
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)



def retry_exc_handler(tries_remaining, exception, delay):
    print("Caught '%s', %d tries remaining, sleeping for %s seconds" % (exception, tries_remaining, delay), file=sys.stderr)

def retries(max_tries, delay=5, backoff=1, exceptions=(Exception,),hook=None):
    def dec(func):
        def f2(*args, **kwargs):
            mydelay = delay
            tries = list(range(max_tries))
            tries.reverse()
            for tries_remaining in tries:
                try:
                    return func(*args, **kwargs)
                except exceptions as e:
                    if tries_remaining > 0:
                        if hook is not None:
                            hook(tries_remaining, e, mydelay)
                        sleep(mydelay)
                        mydelay = mydelay * backoff
                    else:
                        raise
        return f2
    return dec

#Testing if all the objects in the json file are created. If not, create them.

class TestInitData1(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestInitData1, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'fabric_ansible_dir',
                                 "../fabric-ansible/ansible-playbooks")])

    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestInitData1, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass

    def create_object(self, object, res_type, fq_name):
        # Get the class name from object type
        vnc_cls = obj_type_to_vnc_class(res_type, __name__)
        instance_obj = vnc_cls.from_dict(**object)
        try:
            if(res_type == "job-template"):
                schema_name = fq_name.replace('template', 'schema.json')
                with open(os.path.join("../fabric-ansible/ansible-playbooks" +
                                               '/schema/', schema_name),'r+') as schema_file:
                    schema_json = json.load(schema_file)
                    object["job_template_input_schema"] = schema_json.get(
                        "input_schema")
                    object["job_template_output_schema"] = schema_json.get(
                        "output_schema")
                self._vnc_lib.job_template_create(instance_obj)
            else:
                self._vnc_lib._object_create(res_type, instance_obj)
        except RefsExistError:
            pass

    def test_load_init_data_2(self):
        object = {}
        res_type = ""
        fq_name = ""
        try:
            with open("../fabric-ansible/ansible-playbooks/conf"
                      "/predef_payloads.json") as data_file:
                input_json = json.load(data_file)

            for item in input_json.get('data'):
                res_type = item.get("object_type")
                for object in item.get("objects"):
                    fq_name = object.get("name")
                    self._vnc_lib._object_read(res_type=res_type, fq_name=fq_name)
        except NoIdError:
            self.create_object(object, res_type, fq_name)
        except Exception as e:
            print ("Test failed due to unexpected error: %s" % str(e))


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
                }

            ]
        }
        if not os.path.exists("conf"):
            os.makedirs("conf")
        with open("conf/predef_payloads.json", "w") as f:
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
    @retries(5, hook=retry_exc_handler)
    def test_load_init_data_02(self):
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'service-chain-flat-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj):
                jb_list = self._vnc_lib.job_templates_list()
                self.assertEquals(len(jb_list.get('job-templates')), 0)
        except Exception as e:
            print( "Test failed due to unexpected error: %s" % str(e))



# Testing when json is invalid
class TestInitDataError3(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        json_data = "abc"
        if not os.path.exists("conf"):
            os.makedirs("conf")
        with open("conf/predef_payloads.json", "w") as f:
            f.write(json_data)
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
    @retries(5, hook=retry_exc_handler)
    def test_load_init_data_04(self):
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'service-chain-flat-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj):
                jb_list = self._vnc_lib.job_templates_list()
                self.assertEquals(len(jb_list.get('job-templates')), 0)
        except Exception as e:
            print("Test failed due to unexpected error: %s" % str(e))


# Testing when tag type is unknown
class TestInitDataError4(test_case.ApiServerTestCase):
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
        with open("conf/predef_payloads.json", "w") as f:
            json.dump(json_data, f)
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

    @retries(5, hook=retry_exc_handler)
    def test_load_init_data_05(self):
        try:
            ipam_fq_name = ['default-domain', 'default-project',
                            'service-chain-flat-ipam']
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            if (ipam_obj):
                tags = self._vnc_lib.tags_list()
                self.assertEquals(len(tags.get('tags')), 0)
        except Exception as e:
             print("Test failed due to unexpected error: %s" % str(e))

