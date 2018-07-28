#!/usr/bin/python


# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains general utility functions for fabric ansible modules
"""

import uuid
import json
from functools import wraps
from ansible.module_utils.basic import AnsibleModule
from job_manager.fabric_logger import fabric_ansible_logger
from job_manager.job_log_utils import JobLogUtils
from job_manager.sandesh_utils import SandeshUtils


def handle_sandesh(function):
    @wraps(function)
    def wrapper(*args, **kwargs):
        module = args[0]
        try:
            module._validate_job_ctx()
            module.job_log_util = JobLogUtils(
                sandesh_instance_id=str(uuid.uuid4()),
                config_args=json.dumps(module.job_ctx['config_args']))
            function(*args, **kwargs)
        except ValueError as verr:
            module.results['msg'] = str(verr)
            module.results['failed'] = True
            module.logger.error(str(verr))
        except Exception as ex:
            msg = "Failed object log due to error: %s\n\t \
                   job name: %s\n\t \
                   job execution id: %s\n" \
                  % (str(ex), module.job_ctx['job_template_fqname'],
                     module.job_ctx['job_execution_id'])
            module.results['msg'] = msg
            module.results['failed'] = True
            module.logger.error(msg)
        finally:
            try:
                if module.job_log_util:
                    sandesh_util = SandeshUtils(
                        module.job_log_util.get_config_logger())
                    sandesh_util.close_sandesh_connection()
            except Exception as ex:
                module.logger.error(
                    "Unable to close sandesh connection: %s", str(ex))

    return wrapper


#
# Sub-class to provide additional functionality to custom ansible modules
#
class FabricAnsibleModule(AnsibleModule):
    def __init__(self, argument_spec={}, **kwargs):
        super(FabricAnsibleModule, self).__init__(argument_spec=argument_spec,
                                                  **kwargs)
        self.module_name = self._name
        self.job_ctx = self.params.get('job_ctx')
        self.logger = fabric_ansible_logger(self.module_name)
        self.results = dict()
        self.results['failed'] = False
        self.logger.debug("Module params: {}".format(self.params))
        self.job_log_util = None

    def _validate_job_ctx(self):
        required_job_ctx_keys = [
            'job_template_fqname', 'job_execution_id', 'config_args',
            'job_input']
        for key in required_job_ctx_keys:
            if key not in self.job_ctx or self.job_ctx.get(key) is None:
                raise ValueError("Missing job context param: %s" % key)

    @handle_sandesh
    def execute(self, function, *args, **kwargs):
        return function(self, *args, **kwargs)

    def send_prouter_object_log(self, prouter_fqname, onboarding_state,
                                os_version, serial_num):
        try:
            if self.job_log_util:
                self.job_log_util.send_prouter_object_log(
                    prouter_fqname,
                    self.job_ctx['job_execution_id'],
                    json.dumps(self.job_ctx['job_input']),
                    self.job_ctx['job_template_fqname'],
                    onboarding_state,
                    os_version,
                    serial_num)
            else:
                raise Exception("physical router log not initialized")
        except Exception as ex:
            msg = "Failed to create following physical router object " \
                  "log due to error: %s\n\t \
                   job name: %s\n\t \
                   job execution id: %s\n\t \
                   device name: %s\n\t \
                   onboarding_state: %s\n" \
                  % (str(ex), self.job_ctx['job_template_fqname'],
                     self.job_ctx['job_execution_id'], str(prouter_fqname),
                     onboarding_state)
            self.logger.error(msg)

    def send_job_object_log(self, message, status, job_result,
                            log_error_percent=False, job_success_percent=None,
                            job_error_percent=None, device_name=None):
        if job_success_percent is None or (log_error_percent
                                           and job_error_percent is None):
            try:
                total_percent = self.job_ctx.get('playbook_job_percentage')
                if total_percent:
                    total_percent = float(total_percent)
                self.logger.debug(
                    "Calculating the job completion percentage. "
                    "total_task_count: %s, current_task_index: %s, "
                    "playbook_job_percentage: %s,"
                    " task_weightage_array: %s",
                    self.job_ctx.get('total_task_count'),
                    self.job_ctx.get('current_task_index'),
                    total_percent,
                    self.job_ctx.get('task_weightage_array'))
                job_success_percent, job_error_percent = \
                    self.job_log_util.calculate_job_percentage(
                        self.job_ctx.get('total_task_count'),
                        buffer_task_percent=False,
                        task_seq_number=self.job_ctx.get('current_task_index'),
                        total_percent=total_percent,
                        task_weightage_array=
                        self.job_ctx.get('task_weightage_array'))
            except Exception as e:
                self.logger.error("Exception while calculating the job "
                              "percentage %s", str(e))
        if log_error_percent:
            job_percentage = job_error_percent
        else:
            job_percentage = job_success_percent

        self.results['percentage_completed'] = job_percentage
        self.logger.debug("Job complete percentage is %s" % job_percentage)

        try:
            if self.job_log_util:
                self.job_log_util.send_job_log(
                    self.job_ctx['job_template_fqname'],
                    self.job_ctx['job_execution_id'],
                    self.job_ctx['fabric_fqname'],
                    message,
                    status,
                    job_percentage,
                    job_result,
                    device_name=device_name)
            else:
                raise Exception("job log not initialized")
        except Exception as ex:
            msg = "Failed to create following job log due to error: %s\n\t \
                   job name: %s\n\t \
                   job execution id: %s\n\t \
                   fabric uuid: %s\n\t \
                   job percentage: %s\n\t  \
                   job status: %s\n\t, \
                   log message: %s\n" \
                  % (str(ex), self.job_ctx['job_template_fqname'],
                     self.job_ctx['job_execution_id'],
                     self.job_ctx['fabric_fqname'], job_percentage,
                     status, message)
            self.logger.error(msg)

    def calculate_job_percentage(self, num_tasks, buffer_task_percent=False,
                                 task_seq_number=None, total_percent=100,
                                 task_weightage_array=None):
        return self.job_log_util.calculate_job_percentage(
            num_tasks, buffer_task_percent, task_seq_number, total_percent,
            task_weightage_array)

