#!/usr/bin/python


# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains utility functions for sending prouter and job objectlogs
via sandesh
"""

import uuid
import time
import json
import logging
from job_manager.job_log_utils import JobLogUtils
from job_manager.sandesh_utils import SandeshUtils


class ObjectLogUtil(object):

    def __init__(self, job_ctx):
        self.job_ctx = job_ctx
        self.results = dict()
        self.results['failed'] = False
        logging.basicConfig(level=logging.INFO)
        self.validate_job_ctx()

        self.job_log_util = JobLogUtils(
            sandesh_instance_id=str(
                uuid.uuid4()), config_args=json.dumps(
                job_ctx['config_args']))

    def validate_job_ctx(self):
        required_job_ctx_keys = [
            'job_template_fqname', 'job_execution_id', 'config_args',
            'job_input']
        for key in required_job_ctx_keys:
            if key not in self.job_ctx or self.job_ctx.get(key) is None:
                raise ValueError("Missing job context param: %s" % key)

    def send_prouter_object_log(self, prouter_fqname, onboarding_state,
                                os_version, serial_num):
        self.job_log_util.send_prouter_object_log(
            prouter_fqname,
            self.job_ctx['job_execution_id'],
            json.dumps(self.job_ctx['job_input']),
            self.job_ctx['job_template_fqname'],
            onboarding_state,
            os_version,
            serial_num)

    def send_job_object_log(self, message, status, job_result):
        self.job_log_util.send_job_log(
            self.job_ctx['job_template_fqname'],
            self.job_ctx['job_execution_id'],
            message,
            status,
            job_result)

    def close_sandesh_conn(self):
        try:
            sandesh_util = SandeshUtils(self.job_log_util.get_config_logger())
            sandesh_util.close_sandesh_connection()
        except Exception as e:
            logging.error("Unable to close sandesh connection: %s", str(e))

