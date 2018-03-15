#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains the job manager results handler that collects and processes
results from the job executions
"""
import time

from job_utils import JobStatus
from job_exception import JobException


class JobResultHandler(object):

    def __init__(self, job_template, execution_id, logger, job_utils,
                 job_log_utils):
        self._job_template = job_template
        self._execution_id = execution_id
        self._logger = logger
        self._job_utils = job_utils
        self.job_log_utils = job_log_utils

        # job result data
        self.job_result_status = None     # cummulative status
        self.job_result_message = ""   # job result msg when not device spec
        self.job_result = dict()   # map of the device_id to job result msg
        self.job_summary_message = None
        self.failed_device_jobs = list()

    def update_job_status(self, status, message=None, device_id=None):
        # update cummulative job status
        if self.job_result_status is None or \
                        self.job_result_status != JobStatus.FAILURE:
            self.job_result_status = status

        # collect failed device ids
        if status == JobStatus.FAILURE and device_id is not None:
            self.failed_device_jobs.append(device_id)

        # collect the result message
        if message is not None:
            if device_id is not None:
                self.job_result.update({device_id: message})
            else:
                self.job_result_message = message

    def create_job_summary_log(self):
        # generate job result summary
        self.job_summary_message = self.create_job_summary_message()

        timestamp = int(round(time.time()*1000))
        # create the job log
        self._logger.debug("%s" % self.job_summary_message)
        self.job_log_utils.send_job_log(str(self._job_template.get_uuid()),
                                        self._execution_id,
                                        self.job_summary_message,
                                        self.job_result_status.value,
                                        timestamp=timestamp)
        # create the job complete uve
        self.job_log_utils.send_job_execution_uve(
            str(self._job_template.get_uuid()), self._execution_id,
            timestamp, 100)

    def create_job_summary_message(self):
        try:
            job_summary_message = "Job summary: \n"

            if self.job_result_status is None:
                job_summary_message += "Error in getting the job completion " \
                                       "status after job execution. \n"
            elif self.job_result_status == JobStatus.FAILURE:
                if len(self.failed_device_jobs) > 0:
                    job_summary_message += "Job failed with for devices: "
                    for failed_device in self.failed_device_jobs:
                        msg = failed_device + ','
                        job_summary_message += msg
                else:
                    job_summary_message += "Job failed. "
                job_summary_message += "\n"
            elif self.job_result_status == JobStatus.SUCCESS:
                job_summary_message += "Job execution completed " \
                                       "successfully. \n"
            if len(self.job_result) > 0:
                job_summary_message += "Detailed job results: \n"
            result_summary = ""
            for entry in self.job_result:
                result_summary += \
                        "%s:%s \n" % (entry, self.job_result[entry])
            job_summary_message += result_summary

            if self.job_result_message is not None:
                job_summary_message += self.job_result_message

            return job_summary_message
        except Exception as e:
            msg = "Error while generating the job summary " \
                  "message : %s" % repr(e)
            raise JobException(msg, self._execution_id)

