#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used by the job manager
"""
from enum import Enum
import traceback
from decimal import Decimal, getcontext

from job_exception import JobException
from job_messages import MsgBundle


class JobStatus(Enum):
    STARTING = "STARTING"
    IN_PROGRESS = "IN_PROGRESS"
    SUCCESS = "SUCCESS"
    FAILURE = "FAILURE"


class JobUtils(object):

    def __init__(self, job_execution_id=None, job_template_id=None, logger=None, vnc_api=None):
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


    # The api splits the total percentage amongst the tasks and returns the
    # job percentage value per task for both success and failure cases
    # num_tasks - Total number of tasks the job is being split into
    # buffer_task_percent - Set to true if we need to keep aside a buffer
    # value while calculating the per task percentage (to avoid overflows)
    # total_percent - Total percentage alloted to the job, that needs to be
    # split amongst the tasks
    # task_weightage_array - The weightage array which gives the weightage of
    # the tasks for calculating the per task percentage
    # task_seq_number - If the weightage array is provided, then this should
    # indicate the sequence number of the task within the array.
    # Note: The task_seq_number is initialized at 1.
    def calculate_job_percentage(self, num_tasks, buffer_task_percent=False,
                                 task_seq_number=None, total_percent=100,
                                 task_weightage_array=None):
        if num_tasks is None:
            raise JobException("Number of tasks is required to calculate "
                               "the job percentage")

        try:
            getcontext().prec = 3
            # Use buffered approach to mitigate the cumulative task percentages
            # exceeding the total job percentage
            if buffer_task_percent:
                buffer_percent = 0.05 * total_percent
                total_percent -= buffer_percent

            # if task weightage is not provided, the task will recive an
            # equally divided chunk from the total_percent based on the total
            # number of tasks
            if task_weightage_array is None:
                success_task_percent = float(Decimal(total_percent) /
                                             Decimal(num_tasks))
            else:
                if task_seq_number is None:
                    raise JobException("Unable to calculate the task "
                                       "percentage since the task sequence "
                                       "number is not provided")
                success_task_percent = float(Decimal(
                    task_weightage_array[task_seq_number - 1] *
                    total_percent) / 100)

            # based on the task sequence number calculate the percentage to be
            # marked in cases of error. This is required to mark the job to
            # 100% completion in cases of errors when successor tasks will not
            # be executed.
            failed_task_percent = None
            if task_seq_number:
                if task_weightage_array is None:
                    failed_task_percent = (num_tasks - task_seq_number + 1) *\
                        success_task_percent
                else:
                    failed_task_percent = 0.00
                    for task_index in range(task_seq_number, num_tasks):
                        task_percent = float(Decimal(
                            task_weightage_array[task_index - 1] *
                            total_percent) / 100)
                        failed_task_percent += task_percent
            self._logger.info("success_task_percent %s "
                                    "failed_task_percent %s " %
                                    (success_task_percent,
                                     failed_task_percent))

            return success_task_percent, failed_task_percent
        except Exception as e:
            msg = "Exception while calculating the job pecentage %s " % repr(e)
            self._logger.error(msg)
            self._logger.error("%s" % traceback.format_exc())
            raise JobException(e)


