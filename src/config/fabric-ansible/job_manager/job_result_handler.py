#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains the job manager results handler that collects and processes
results from the job executions
"""

import traceback
import json

from job_utils import JobStatus
from job_exception import JobException


class JobResultHandler(object):

    DUMMY_DEVICE_ID = 'DummyDeviceId'

    class DeviceJobResult(object):
        def __init__(self, message, job_output):
            self.message = message
            self.job_output = job_output

    def __init__(self, job_template, execution_id, logger, job_utils):
        self._job_template = job_template
        self._execution_id = execution_id
        self._sandesh = logger._sandesh
        self._logger = logger
        self._job_utils = job_utils

        # job result data
        self._job_result_status = None     # cummulative status
        self._job_result_message = ""   # cummulative message
        self._job_result = dict()   # map of the device if to DeviceJobResult
        self._failed_device_jobs = list()

    def update_job_status(self, output, device_id=None):
        try:
            output = json.loads(output)
            # fetch the status and message from output
            message = output.get('message')
            status = output.get('status')

            # update the cummulative job status
            if status == "Success":
                job_status = JobStatus.SUCCESS
            elif status == "Failure":
                job_status = JobStatus.FAILURE
            else:
                job_status = JobStatus.FAILURE
                err_msg = " No completion status received from the playbook." \
                          " Marking the job as failed."
                if message is not None:
                    message += err_msg
                else:
                    message = err_msg

            if self._job_result_status is None or \
                    self._job_result_status == JobStatus.SUCCESS:
                self._job_result_status = job_status

            # update the result
            if device_id is None:
                device_id = self.DUMMY_DEVICE_ID
            self._job_result.update({device_id: self.DeviceJobResult(
                message, output)})
        except Exception as e:
            msg = "Error while updating the job status after playbook " \
                  "execution : %s" % repr(e)
            raise JobException(msg, self._execution_id)

    def create_job_summary_log(self, timestamp):
        # generate job result summary
        self._job_result_message = self.create_job_summary_message()
        # create the job log
        self._job_utils.send_job_log(self._job_result_message,
                                     self._job_result_status,
                                     timestamp=timestamp)

    def create_job_summary_message(self):
        try:
            job_summary_message = "Job summary: \n"

            if len(self._failed_device_jobs) > 0:
                job_summary_message += "Job failed for devices: "
                for failed_device in self._failed_device_jobs:
                    msg = failed_device + ','
                    job_summary_message += msg
                job_summary_message += "\n"
            else:
                job_summary_message += "Job is successful \n"

            job_summary_message += "Detailed job results: \n"
            result_summary = ""
            for entry in self._job_result:
                if entry == self.DUMMY_DEVICE_ID:
                    result_summary += self._job_result[
                        self.DUMMY_DEVICE_ID].message
                else:
                    result_summary += \
                        "%s:%s \n" % (entry, self._job_result[
                            self.DUMMY_DEVICE_ID].message)
            job_summary_message += result_summary

            return job_summary_message
        except Exception as e:
            msg = "Error while generating the job summary " \
                  "message : %s" % repr(e)
            raise JobException(msg, self._execution_id)
