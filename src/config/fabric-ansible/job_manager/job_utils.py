#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used by the job manager
"""
from enum import Enum

from job_exception import JobException
from job_error_messages import get_job_template_read_error_message


class JobStatus(Enum):
    STARTING = 0
    IN_PROGRESS = 1
    SUCCESS = 2
    FAILURE = 3
    TIMEOUT = 4


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
            msg = get_job_template_read_error_message(self._job_template_id)
            raise JobException(msg, self._job_execution_id)
        return job_template
