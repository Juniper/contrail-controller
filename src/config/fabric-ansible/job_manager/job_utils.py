#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used by the job manager
"""
import random
from enum import Enum

from vnc_api.vnc_api import VncApi

from job_exception import JobException
from job_messages import MsgBundle


class JobStatus(Enum):
    STARTING = "STARTING"
    IN_PROGRESS = "IN_PROGRESS"
    SUCCESS = "SUCCESS"
    FAILURE = "FAILURE"

class JobVncApi(object):
    @staticmethod
    def vnc_init(job_ctx):
        host = random.choice(job_ctx.get('api_server_host'))
        return VncApi(
            api_server_host=host,
            auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
            auth_token=job_ctx.get('auth_token')
        )

class JobUtils(object):

    def __init__(self, job_execution_id, job_template_id, logger, vnc_api):
        self._job_execution_id = job_execution_id
        self._job_template_id = job_template_id
        self._logger = logger
        self._vnc_api = vnc_api

    def read_job_template(self):
        try:
            job_template = self._vnc_api.job_template_read(
                id=self._job_template_id)
            self._logger.debug("Read job template %s from "
                               "database" % self._job_template_id)
        except Exception as e:
            msg = MsgBundle.getMessage(MsgBundle.READ_JOB_TEMPLATE_ERROR,
                                       job_template_id=self._job_template_id)
            raise JobException(msg, self._job_execution_id)
        return job_template

