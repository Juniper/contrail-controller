#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)
import sys
import uuid
import os
import logging
from flexmock import flexmock
from ansible.executor.playbook_executor import PlaybookExecutor

from vnc_api.vnc_api import *

sys.path.append('../common/tests')
from test_utils import *
import test_case
from job_mgr import JobManager, initialize_sandesh_logger

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestJobManager(test_case.JobTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestJobManager, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestJobManager, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_execute_job(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper',
                                     device_family='MX')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_executor()
        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "execution_id": str(execution_id)}
        sandesh_logger = initialize_sandesh_logger()
        jm = JobManager(sandesh_logger, self._vnc_lib, job_input_json)
        jm.start_job()

    def mock_play_book_executor(self):
        facts_dict = {"localhost":
                         {"output":
                             {"status": "Success",
                              "message": "Playbook executed successfully",
                              "results": {"discovered_pr_id": ["123"]}}}}
        mocked_var_mgr = flexmock(_nonpersistent_fact_cache=facts_dict)
        mocked_task_q_mgr = flexmock(_variable_manager=mocked_var_mgr)
        mocked_playbook_executor = flexmock(_tqm=mocked_task_q_mgr)
        flexmock(PlaybookExecutor, __new__=mocked_playbook_executor)
        mocked_playbook_executor.should_receive('__init__')
        mocked_playbook_executor.should_receive('run')

        flexmock(os.path).should_receive('exists').and_return(True)

