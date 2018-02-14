#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used by the job manager
"""

from enum import Enum
from time import gmtime, strftime
import traceback

from sandesh.job.ttypes import *

from job_exception import JobException


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

    def send_job_log(self, message, status, result=None, timestamp=None):
        try:
            if timestamp is None:
                timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            job_log_entry = JobLogEntry(name=self._job_template_id,
                                        execution_id=self._job_execution_id,
                                        timestamp=timestamp, message=message,
                                        status=status, result=result)
            job_log = JobLog(log_entry=job_log_entry)
            job_log.send(sandesh=self._logger._sandesh)
            self._logger.debug("Created job log for job template: %s, "
                               "execution id: %s,  status: %s, result: %s,"
                               " message: %s" % (self._job_template_id,
                                                 self._job_execution_id,
                                                 status, result, message))
        except Exception as e:
            msg = "Error while creating the job log for job template " \
                  "%s and execution id %s : %s" % (self._job_template_id,
                                                   self._job_execution_id,
                                                   repr(e))
            raise JobException(msg, self._job_execution_id)

    def send_job_execution_uve(self, timestamp=None,
                               percentage_completed=None):
        try:
            if timestamp is None:
                timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            job_exe_data = JobExecution(
                name=self._job_template_id,
                execution_id=self._job_execution_id,
                job_start_ts=timestamp,
                percentage_completed=percentage_completed)
            job_uve = UveJobExecution(data=job_exe_data)
            job_uve.send(sandesh=self._logger._sandesh)
        except Exception as e:
            msg = "Error while sending the job execution UVE for job " \
                  "template %s and execution id %s : %s" % \
                  (self._job_template_id, self._job_execution_id, repr(e))
            raise JobException(msg, self._job_execution_id)

    def read_job_template(self):
        try:
            job_template = self._vnc_api.job_template_read(
                id=self._job_template_id)
            self._logger.debug("Read job template %s from "
                               "database" % self._job_template_id)
        except Exception as e:
            msg = "Error while reading the job template %s from " \
                  "database" % self._job_template_id
            raise JobException(msg, self._job_execution_id)
        return job_template

    def get_device_family(self, device_id):
        try:
            pr = self._vnc_api.physical_router_read(id=device_id)
            self._logger.debug("Read device family as %s for device "
                               "%s" % (pr.get_physical_router_device_family(),
                                       device_id))
        except Exception as e:
            msg = "Error while reading the device family from DB for " \
                  "device %s " % device_id
            raise JobException(msg, self._job_execution_id)

        return pr.get_physical_router_device_family()
