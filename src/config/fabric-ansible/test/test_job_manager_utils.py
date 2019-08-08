from builtins import object
import json
from flexmock import flexmock

from vnc_api.vnc_api import PlaybookInfoType
from vnc_api.vnc_api import PlaybookInfoListType

from job_manager.job_log_utils import JobLogUtils
from job_manager.sandesh_utils import SandeshUtils


class TestJobManagerUtils(object):

    play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                 vendor='Juniper')
    playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
    execution_id = "6d69febf-82da-4998-8ba9-dbff8f6e7a5b"
    args = {"collectors": ['127.0.0.1:8086'], "host_ip": '127.0.0.1'}
    fake_schema = {
                   "$schema": "http://json-schema.org/draft-04/schema#",
                   "title": "InventoryItem",
                   "type": "object",
                   "properties": {
                     "name": {
                       "type": "string"
                     },
                     "price": {
                       "type": "number",
                       "minimum": 0
                     },
                     "sku": {
                       "description": "Stock Keeping Unit",
                       "type": "integer"
                     }
                   },
                   "required": ["name", "price"]
                  }

    @classmethod
    def get_min_details(cls, job_template_id):
        job_input_json = {
                          "job_template_id": job_template_id,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": TestJobManagerUtils.execution_id,
                          "fabric_fq_name": "Global-system-config:fabric-1",
                          "auth_token": "6e7d7f87faa54fac96a2a28ec752336a",
                          "api_server_host": ['1.1.1.1'],
                          "args": TestJobManagerUtils.args
                         }
        log_utils = JobLogUtils(
                        sandesh_instance_id=TestJobManagerUtils.execution_id,
                        config_args=json.dumps(TestJobManagerUtils.args))
        return job_input_json, log_utils

    @classmethod
    def mock_sandesh_check(cls):
        mocked_sandesh_utils = flexmock()
        flexmock(SandeshUtils, __new__=mocked_sandesh_utils)
        mocked_sandesh_utils.should_receive('__init__')
        mocked_sandesh_utils.should_receive('wait_for_connection_establish')
        mocked_sandesh_utils.should_receive('close_sandesh_connection')

