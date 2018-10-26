#!/usr/bin/python


# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains general utility functions for fabric ansible modules
"""

import uuid
import json
import traceback
from functools import wraps
from ansible.module_utils.basic import AnsibleModule
from job_manager.fabric_logger import fabric_ansible_logger
from job_manager.job_log_utils import JobLogUtils
from job_manager.sandesh_utils import SandeshUtils

MARKER_JOB_LOG = "JOB_LOG##"
MARKER_PROUTER_LOG = "PROUTER_LOG##"

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
        prouter_log = {
            'prouter_fqname': prouter_fqname,
            'job_execution_id': self.job_ctx.get('job_execution_id'),
            'job_input': json.dumps(self.job_ctx['job_input']),
            'job_template_fqname': self.job_ctx['job_template_fqname'],
            'onboarding_state': onboarding_state,
            'os_version': os_version,
            'serial_num': serial_num
        }
        self._write_to_file(self.job_ctx, MARKER_PROUTER_LOG, prouter_log)

    def send_job_object_log(self, message, status, job_result,
                            log_error_percent=False, job_success_percent=None,
                            job_error_percent=None, device_name=None,
                            details=None):
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

        job_log = {
            'job_template_fqname': self.job_ctx.get('job_template_fqname'),
            'job_execution_id': self.job_ctx.get('job_execution_id'),
            'fabric_fq_name': self.job_ctx.get('fabric_fqname'),
            'message': message,
            'status': status,
            'completion_percent': job_percentage,
            'result': job_result,
            'device_name': device_name,
            'details': details
        }
        self._write_to_file(self.job_ctx, MARKER_JOB_LOG, job_log)

    def _write_to_file(self, job_ctx, marker, obj_log):
        exec_id = str(job_ctx.get('job_execution_id'))
        try:
            fname = '/tmp/%s' % exec_id
            with open(fname, "a") as f:
                line_in_file = "%s%s%s\n" % (
                    marker, json.dumps(obj_log), marker
                )
                f.write(line_in_file)
        except Exception as ex:
            self._logger.error(
                "Failed to write job log due to error: %s\njob log: %s\n%s" % (
                    str(ex), json.dumps(obj_log, indent=4),
                    traceback.format_exc()
                )
            )

    def calculate_job_percentage(self, num_tasks, buffer_task_percent=False,
                                 task_seq_number=None, total_percent=100,
                                 task_weightage_array=None):
        return self.job_log_util.calculate_job_percentage(
            num_tasks, buffer_task_percent, task_seq_number, total_percent,
            task_weightage_array)

