#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Job results handler that collects and processes results from the job."""

from builtins import object
import json
import time

from job_manager.job_messages import MsgBundle
from job_manager.job_utils import JobFileWrite, JobStatus


class JobResultHandler(object):

    def __init__(self, job_template_id, execution_id, fabric_fq_name,
                 logger, job_utils, job_log_utils, device_name,
                 job_description, transaction_id, transaction_descr):
        """Initializes JobResultHandler."""
        self._job_template_id = job_template_id
        self._execution_id = execution_id
        self._fabric_fq_name = fabric_fq_name
        self._logger = logger
        self._job_utils = job_utils
        self.job_log_utils = job_log_utils
        self._device_name = device_name
        self._job_description = job_description
        self._transaction_id = transaction_id
        self._transaction_descr = transaction_descr

        # job result data
        self.job_result_status = None     # cummulative status
        self.job_result_message = ""   # job result msg when not device spec
        self.job_warning_message = ""  # job warning msg when not device spec
        self.job_result = dict()   # map of the device_id to job result msg
        self.job_summary_message = None
        self.failed_device_jobs = list()
        self.warning_device_jobs = set()
        # device_management_ip, device_username, etc
        self.playbook_output = None  # marked output from the playbook stdout
        self.percentage_completed = 0.0
        self._job_file_write = JobFileWrite(self._logger)

    def get_retry_devices(self):
        return (self.playbook_output or {}).get('retry_devices')

    def get_failed_device_list(self):
        return (self.playbook_output or {}).get('failed_list')

    def update_job_status(
            self, status, message=None, device_id=None,
            device_name=None, pb_results=None):
        # update cummulative job status
        if self.job_result_status is None or \
                self.job_result_status != JobStatus.FAILURE:
            self.job_result_status = status

        # collect failed device ids
        if status == JobStatus.FAILURE and device_id is not None:
            self.failed_device_jobs.append(device_id)

        if status == JobStatus.WARNING:
            if device_id is not None:
                self.warning_device_jobs.add(device_id)
                if device_id in self.job_result:
                    exis_warn_mess = self.job_result[device_id].get(
                        "warning_message", ""
                    )
                    self.job_result[device_id].update(
                        {
                            "warning_message": exis_warn_mess + message
                        }
                    )
                else:
                    self.job_result[device_id] = {
                        "warning_message": message
                    }
            else:
                self.job_warning_message += message + "\n"
        else:
            # collect the result message
            if message is not None:
                if device_id is not None:
                    if device_id in self.job_result:
                        self.job_result[device_id].update(
                            {
                                "message": message,
                                "device_name": device_name,
                                "device_op_result": pb_results
                            }
                        )
                    else:
                        self.job_result.update(
                            {device_id: {"message": message,
                                         "device_name": device_name,
                                         "device_op_result": pb_results}})
                else:
                    self.job_result_message = message
    # end update_job_status

    def update_playbook_output(self, pb_output):
        if self.playbook_output:
            self.playbook_output.update(pb_output)
        else:
            self.playbook_output = pb_output
    # end update_playbook_output

    def create_job_summary_log(self, job_template_fqname):
        # generate job result summary
        self.job_summary_message, device_op_results, failed_device_names = \
            self.create_job_summary_message()

        result = {"gen_dev_job_op": json.dumps(device_op_results)} \
            if device_op_results else None

        timestamp = int(round(time.time() * 1000))
        # create the job log
        self._logger.debug("%s" % self.job_summary_message)
        job_status = None
        if self.job_result_status:
            job_status = self.job_result_status.value
        # write to the file as well
        file_write_data = {
            "job_status": job_status,
            "failed_devices_list": failed_device_names
        }
        self._job_file_write.write_to_file(
            self._execution_id,
            "job_summary",
            JobFileWrite.JOB_LOG,
            file_write_data)

        self.job_log_utils.send_job_log(
            job_template_fqname,
            self._execution_id,
            self._fabric_fq_name,
            self.job_summary_message,
            job_status, 100,
            result=result,
            timestamp=timestamp,
            device_name=self._device_name,
            description=self._job_description,
            transaction_id=self._transaction_id,
            transaction_descr=self._transaction_descr)
    # end create_job_summary_log

    def create_job_summary_message(self):
        job_summary_message = MsgBundle.getMessage(
            MsgBundle.JOB_SUMMARY_MESSAGE_HDR)

        failed_device_jobs_len = len(self.failed_device_jobs)

        if self.job_result_status is None:
            job_summary_message += MsgBundle.getMessage(
                MsgBundle.JOB_RESULT_STATUS_NONE)
        elif self.job_result_status == JobStatus.FAILURE:
            if failed_device_jobs_len > 0:
                job_summary_message += MsgBundle.getMessage(
                    MsgBundle.
                    JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR)
                for failed_device in self.failed_device_jobs:
                    msg = failed_device + ','
                    job_summary_message += msg
            else:
                job_summary_message += "Job failed. "
            job_summary_message += "\n"
        elif self.job_result_status == JobStatus.SUCCESS:
            if failed_device_jobs_len > 0:
                self.job_result_status = JobStatus.WARNING
            job_summary_message += MsgBundle.getMessage(
                MsgBundle.JOB_EXECUTION_COMPLETE)
        device_job_result_len = len(self.job_result)
        if device_job_result_len > 0:
            job_summary_message += MsgBundle.getMessage(
                MsgBundle.PLAYBOOK_RESULTS_MESSAGE)
            job_summary_message += "Successfully completed "\
                                   "job for %s devices.\n"\
                                   % (device_job_result_len -
                                      failed_device_jobs_len)
        # result_summary would infact be the failed_devices
        # result summary
        # warning_summary is warning for multi device jobs
        result_summary = ""
        device_op_results = []
        failed_device_names = []
        warning_summary = ""

        for entry in self.job_result:
            if entry in self.failed_device_jobs:
                result_summary += \
                    "%s:%s \n" % (self.job_result[entry]['device_name'],
                                  self.job_result[entry]['message'])
                failed_device_names.append(
                    self.job_result[entry]['device_name'])
            elif self.job_result[entry]['device_op_result']:
                # could be other device jobs such as device import, topology
                device_op_results.append(
                    self.job_result[entry]['device_op_result'])
            if entry in self.warning_device_jobs:
                warning_summary += \
                    "%s: %s \n" % (self.job_result[entry]['device_name'],
                                   self.job_result[entry]['warning_message'])

        if result_summary != "":
            failed_device_msg = "Job execution failed for %s devices.\n"\
                                % len(self.failed_device_jobs)
            result_summary = failed_device_msg + result_summary
        job_summary_message += result_summary

        if self.job_result_message is not None:
            job_summary_message += self.job_result_message

        if self.job_warning_message or self.warning_device_jobs:
            job_summary_message += "\nJob execution had the following" \
                                   " warnings: \n"
            job_summary_message += self.job_warning_message
            job_summary_message += warning_summary

        return job_summary_message, device_op_results, failed_device_names
    # end create_job_summary_message
