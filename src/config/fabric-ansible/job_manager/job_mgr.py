#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager process code and api
"""
import time
import sys
import json
import jsonschema
import argparse
import traceback

from job_manager.job_handler import JobHandler
from job_manager.job_exception import JobException
from job_manager.job_log_utils import JobLogUtils
from job_manager.job_utils import JobUtils, JobStatus
from job_manager.job_result_handler import JobResultHandler
from job_manager.job_messages import MsgBundle
from job_manager.sandesh_utils import SandeshUtils

from vnc_api.vnc_api import VncApi
from gevent.greenlet import Greenlet
from gevent.pool import Pool
from gevent import monkey
monkey.patch_socket()


class JobManager(object):

    def __init__(self, logger, vnc_api, job_input, job_log_utils, job_template,
                 result_handler, job_utils, playbook_seq, job_percent):
        self._logger = logger
        self._vnc_api = vnc_api
        self.job_execution_id = None
        self.job_data = None
        self.job_params = dict()
        self.device_json = None
        self.auth_token = None
        self.job_log_utils = job_log_utils
        self.job_template = job_template
        self.sandesh_args = None
        self.max_job_task = JobLogUtils.TASK_POOL_SIZE
        self.fabric_fq_name = None
        self.parse_job_input(job_input)
        self.job_utils = job_utils
        self.playbook_seq = playbook_seq
        self.result_handler = result_handler
        self.job_percent = job_percent
        logger.debug("Job manager initialized")

    def parse_job_input(self, job_input_json):

        self.job_execution_id = job_input_json['job_execution_id']

        self.job_data = job_input_json.get('input')
        if self.job_data is None:
            self._logger.debug("Job input data is not provided.")

        self.job_params = job_input_json.get('params')
        if self.job_params is None:
            self._logger.debug("Job extra params is not present.")

        self.device_json = job_input_json.get('device_json')
        if self.device_json is None:
            self._logger.debug("Device data is not passed from api server.")

        self.auth_token = job_input_json['auth_token']

        self.sandesh_args = job_input_json['args']
        self.max_job_task = self.job_log_utils.args.max_job_task

        self.fabric_fq_name = job_input_json.get('fabric_fq_name')
        self.prev_pb_output = job_input_json.get('prev_pb_output') or {}

    def start_job(self):
        # spawn job greenlets
        job_handler = JobHandler(self._logger, self._vnc_api,
                                 self.job_template, self.job_execution_id,
                                 self.job_data, self.job_params,
                                 self.job_utils, self.device_json,
                                 self.auth_token, self.job_log_utils,
                                 self.sandesh_args, self.fabric_fq_name,
                                 self.job_log_utils.args.playbook_timeout,
                                 self.playbook_seq, self.prev_pb_output)

        if self.device_json and len(self.device_json) >= 1:
            self.handle_multi_device_job(job_handler, self.result_handler)
        else:
            self.handle_single_job(job_handler, self.result_handler)

    def handle_multi_device_job(self, job_handler, result_handler):

        job_worker_pool = Pool(self.max_job_task)
        job_percent_per_task = \
            self.job_log_utils.calculate_job_percentage(
                len(self.device_json), buffer_task_percent=False,
                total_percent=self.job_percent)[0]
        for device_id in self.device_json:
            device_data = self.device_json.get(device_id)
            device_fqname = ':'.join(
                map(str, device_data.get('device_fqname')))
            job_template_fq_name = ':'.join(
                map(str, self.job_template.fq_name))
            pr_fabric_job_template_fq_name = device_fqname + ":" + \
                self.fabric_fq_name + ":" + \
                job_template_fq_name
            self.job_log_utils.send_prouter_job_uve(
                self.job_template.fq_name,
                pr_fabric_job_template_fq_name,
                self.job_execution_id,
                job_status="JOB_IN_PROGRESS")

            job_worker_pool.start(Greenlet(job_handler.handle_job,
                                           result_handler,
                                           job_percent_per_task, device_id))
        job_worker_pool.join()

    def handle_single_job(self, job_handler, result_handler):
        job_percent_per_task = self.job_log_utils.calculate_job_percentage(
            1, buffer_task_percent=False, total_percent=self.job_percent)[0]
        job_handler.handle_job(result_handler, job_percent_per_task)


class WFManager(object):

    def __init__(self, logger, vnc_api, job_input, job_log_utils):
        self._logger = logger
        self._vnc_api = vnc_api
        self.job_input = job_input
        self.job_log_utils = job_log_utils
        self.job_execution_id = None
        self.job_template_id = None
        self.device_json = None
        self.result_handler = None
        self.job_data = None
        self.parse_job_input(job_input)
        self.job_utils = JobUtils(self.job_execution_id,
                                  self.job_template_id,
                                  self._logger, self._vnc_api)
        logger.debug("Job manager initialized")

    def parse_job_input(self, job_input_json):
        # job input should have job_template_id and execution_id field
        if job_input_json.get('job_template_id') is None:
            msg = MsgBundle.getMessage(MsgBundle.JOB_TEMPLATE_MISSING)
            raise Exception(msg)

        if job_input_json.get('job_execution_id') is None:
            msg = MsgBundle.getMessage(
                MsgBundle.JOB_EXECUTION_ID_MISSING)
            raise Exception(msg)

        self.job_template_id = job_input_json.get('job_template_id')
        self.job_execution_id = job_input_json['job_execution_id']
        self.job_data = job_input_json.get('input')
        self.fabric_fq_name = job_input_json.get('fabric_fq_name')

    def _validate_job_input(self, input_schema, ip_json):
        if ip_json is None:
            msg = MsgBundle.getMessage(
                MsgBundle.INPUT_SCHEMA_INPUT_NOT_FOUND)
            raise JobException(msg,
                               self.job_execution_id)
        try:
            ip_schema_json = input_schema
            if isinstance(input_schema, str):
                ip_schema_json = json.loads(input_schema)
            jsonschema.validate(ip_json, ip_schema_json)
            self._logger.debug("Input Schema Validation Successful"
                               "for template %s" % self.job_template_id)
        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.INVALID_SCHEMA,
                                       job_template_id=self.job_template_id,
                                       exc_obj=exp)
            raise JobException(msg, self.job_execution_id)

    def start_job(self):
        job_error_msg = None
        job_template = None
        try:
            # create job UVE and log
            msg = MsgBundle.getMessage(MsgBundle.START_JOB_MESSAGE,
                                       job_execution_id=self.job_execution_id)
            self._logger.debug(msg)

            job_template = self.job_utils.read_job_template()

            timestamp = int(round(time.time() * 1000))
            self.job_log_utils.send_job_log(job_template.fq_name,
                                            self.job_execution_id,
                                            self.fabric_fq_name,
                                            msg,
                                            JobStatus.STARTING.value,
                                            timestamp=timestamp)

            self.result_handler = JobResultHandler(self.job_template_id,
                                                   self.job_execution_id,
                                                   self.fabric_fq_name,
                                                   self._logger,
                                                   self.job_utils,
                                                   self.job_log_utils)

            # validate job input if required by job_template input_schema
            input_schema = job_template.get_job_template_input_schema()
            if input_schema:
                self._validate_job_input(input_schema, self.job_data)

            playbook_list = job_template.get_job_template_playbooks()\
                .get_playbook_info()

            job_percent = None
            # calculate job percentage for each playbook
            if len(playbook_list) > 1:
                task_weightage_array = [
                    pb_info.job_completion_weightage
                    for pb_info in playbook_list]

            for i in range(0, len(playbook_list)):

                if len(playbook_list) > 1:
                    # get the job percentage based on weightage of each plabook
                    # when they are chained
                    job_percent = \
                        self.job_log_utils.calculate_job_percentage(
                            len(playbook_list), buffer_task_percent=True,
                            total_percent=100, task_seq_number=i + 1,
                            task_weightage_array=task_weightage_array)[0]
                else:
                    job_percent = \
                        self.job_log_utils.calculate_job_percentage(
                            len(playbook_list), buffer_task_percent=True,
                            total_percent=100)[0]  # using equal weightage

                job_mgr = JobManager(self._logger, self._vnc_api,
                                     self.job_input, self.job_log_utils,
                                     job_template,
                                     self.result_handler, self.job_utils, i,
                                     job_percent)

                job_mgr.start_job()

                # stop the workflow if playbook failed
                if self.result_handler.job_result_status == JobStatus.FAILURE:
                    self._logger.error(
                        "Stop the workflow on the failed Playbook.")
                    break

                # read the device_data output of the playbook
                # and update the job input so that it can be used in next
                # iteration
                if not self.job_input.get('device_json'):
                    device_json = self.result_handler.get_device_data()
                    self.job_input['device_json'] = device_json

                # update the job input with marked playbook output json
                pb_output = self.result_handler.playbook_output or {}
                if not self.job_input.get('prev_pb_output'):
                    self.job_input['prev_pb_output'] = pb_output
                else:
                    self.job_input['prev_pb_output'].update(pb_output)
                self.job_input.get('input', {}).update(pb_output)

            # create job completion log and update job UVE
            self.result_handler.create_job_summary_log(
                job_template.fq_name)

            # in case of failures, exit the job manager process with failure
            if self.result_handler.job_result_status == JobStatus.FAILURE:
                job_error_msg = self.result_handler.job_summary_message

        except JobException as exp:
            err_msg = "Job Exception recieved: %s " % repr(exp)
            self._logger.error(err_msg)
            self._logger.error("%s" % traceback.format_exc())
            self.result_handler.update_job_status(JobStatus.FAILURE,
                                                  err_msg)
            if job_template:
                self.result_handler.create_job_summary_log(
                    job_template.fq_name)
            job_error_msg = err_msg
        except Exception as exp:
            err_msg = "Error while executing job %s " % repr(exp)
            self._logger.error(err_msg)
            self._logger.error("%s" % traceback.format_exc())
            self.result_handler.update_job_status(JobStatus.FAILURE,
                                                  err_msg)
            self.result_handler.create_job_summary_log(job_template.fq_name)
            job_error_msg = err_msg
        finally:
            # need to wait for the last job log and uve update to complete
            # via sandesh and then close sandesh connection
            sandesh_util = SandeshUtils(self._logger)
            sandesh_util.close_sandesh_connection()
            self._logger.info("Closed Sandesh connection")
            if job_error_msg is not None:
                sys.exit(job_error_msg)


def parse_args():
    parser = argparse.ArgumentParser(description='Job manager parameters')
    parser.add_argument('-i', '--job_input', nargs=1,
                        help='Job manager input json')
    return parser.parse_args()


if __name__ == "__main__":

    # parse the params passed to the job manager process and initialize
    # sandesh instance
    job_input_json = None
    try:
        job_params = parse_args()
        job_input_json = json.loads(job_params.job_input[0])
        if job_input_json is None:
            sys.exit("Job input data is not passed to job mgr. "
                     "Aborting job ...")

        job_log_utils = JobLogUtils(
            sandesh_instance_id=job_input_json['job_execution_id'],
            config_args=job_input_json['args'])
        logger = job_log_utils.config_logger
    except Exception as exp:
        print >> sys.stderr, "Failed to initialize logger due "\
                             "to Exception: %s" % traceback.format_exc()
        sys.exit(
            "Exiting due to logger initialization error: %s" % repr(exp))

    # initialize _vnc_api instance
    vnc_api = None
    try:
        auth_token = job_input_json['auth_token']
        vnc_api = VncApi(auth_token=auth_token)
        logger.info("VNC api is initialized using the auth token passed.")
    except Exception as exp:
        logger.error(MsgBundle.getMessage(MsgBundle.VNC_INITIALIZATION_ERROR,
                                          exc_msg=traceback.format_exc()))
        msg = MsgBundle.getMessage(MsgBundle.VNC_INITIALIZATION_ERROR,
                                   exc_msg=repr(exp))
        job_log_utils.send_job_log(job_input_json['job_template_fq_name'],
                                   job_input_json['job_execution_id'],
                                   job_input_json.get('fabric_fq_name'),
                                   msg, JobStatus.FAILURE)
        sys.exit(msg)

    # invoke job manager
    try:
        workflow_manager = WFManager(logger, vnc_api, job_input_json,
                                     job_log_utils)
        logger.info("Job Manager is initialized. Starting job.")
        workflow_manager.start_job()
    except Exception as exp:
        logger.error(MsgBundle.getMessage(MsgBundle.JOB_ERROR,
                                          exc_msg=traceback.format_exc()))
        msg = MsgBundle.getMessage(MsgBundle.JOB_ERROR,
                                   exc_msg=repr(exp))
        job_log_utils.send_job_log(job_input_json['job_template_fq_name'],
                                   job_input_json['job_execution_id'],
                                   job_input_json.get('fabric_fq_name'),
                                   msg, JobStatus.FAILURE)
        sys.exit(msg)
