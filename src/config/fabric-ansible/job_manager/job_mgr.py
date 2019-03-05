#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""This file contains job manager process code and api."""

import argparse
import json
import socket
import signal
import sys
import time
import traceback

from cfgm_common.zkclient import ZookeeperClient
from gevent import monkey
monkey.patch_socket()
from gevent.greenlet import Greenlet
from gevent.pool import Pool
import jsonschema
from vnc_api.vnc_api import VncApi

from job_manager.job_exception import JobException
from job_manager.job_handler import JobHandler
from job_manager.job_log_utils import JobLogUtils
from job_manager.job_messages import MsgBundle
from job_manager.job_result_handler import JobResultHandler
from job_manager.job_utils import JobStatus, JobUtils
from job_manager.sandesh_utils import SandeshUtils


class JobManager(object):

    def __init__(self, logger, vnc_api, job_input, job_log_utils, job_template,
                 result_handler, job_utils, playbook_seq, job_percent,
                 zk_client):
        """Initializes job manager."""
        self._logger = logger
        self._vnc_api = vnc_api
        self.job_execution_id = None
        self.job_data = None
        self.device_json = None
        self.auth_token = None
        self.contrail_cluster_id = None
        self.api_server_host = None
        self.job_log_utils = job_log_utils
        self.job_template = job_template
        self.sandesh_args = None
        self.max_job_task = JobLogUtils.TASK_POOL_SIZE
        self.fabric_fq_name = None
        self.vnc_api_init_params = None
        self.parse_job_input(job_input)
        self.job_utils = job_utils
        self.playbook_seq = playbook_seq
        self.result_handler = result_handler
        self.job_percent = job_percent
        self._zk_client = zk_client
        self.job_handler = None
        logger.debug("Job manager initialized")

    def parse_job_input(self, job_input_json):

        self.job_execution_id = job_input_json.get('job_execution_id')

        self.job_data = job_input_json.get('input')
        if self.job_data is None:
            self._logger.debug("Job input data is not provided.")

        self.device_json = job_input_json.get('device_json')
        if self.device_json is None:
            self._logger.debug("Device data is not passed from api server.")

        self.auth_token = job_input_json.get('auth_token')
        self.contrail_cluster_id = job_input_json.get('contrail_cluster_id')
        self.api_server_host = job_input_json.get('api_server_host')

        self.sandesh_args = job_input_json.get('args')
        self.max_job_task = self.job_log_utils.args.max_job_task

        self.fabric_fq_name = job_input_json.get('fabric_fq_name')

        self.vnc_api_init_params = job_input_json.get('vnc_api_init_params')

        self.total_job_task_count = self.job_data.get('total_job_task_count')

    def start_job(self):
        # spawn job greenlets
        job_handler = JobHandler(self._logger, self._vnc_api,
                                 self.job_template, self.job_execution_id,
                                 self.job_data, self.job_utils,
                                 self.device_json, self.auth_token,
                                 self.contrail_cluster_id,
                                 self.api_server_host, self.job_log_utils,
                                 self.sandesh_args, self.fabric_fq_name,
                                 self.job_log_utils.args.playbook_timeout,
                                 self.playbook_seq, self.vnc_api_init_params,
                                 self._zk_client)
        self.job_handler = job_handler

        # check if its a multi device playbook
        playbooks = self.job_template.get_job_template_playbooks()
        play_info = playbooks.playbook_info[self.playbook_seq]
        is_multi_device_playbook = play_info.multi_device_playbook

        # for fabric config push as part of delete workflow,
        # device json is not needed. There will be no performance
        # impact as fabric delete from DM will always have one prouter
        # uuid in the device_list.
        if is_multi_device_playbook:
            if self.device_json is None or not self.device_json:
                msg = MsgBundle.getMessage(MsgBundle.DEVICE_JSON_NOT_FOUND)
                raise JobException(msg, self.job_execution_id)
            else:
                self.handle_multi_device_job(job_handler, self.result_handler)
        else:
            self.handle_single_job(job_handler, self.result_handler)

    def handle_multi_device_job(self, job_handler, result_handler):

        job_worker_pool = Pool(self.max_job_task)
        job_percent_per_task = \
            self.job_log_utils.calculate_job_percentage(
                self.total_job_task_count or len(self.device_json),
                buffer_task_percent=False,
                total_percent=self.job_percent)[0]
        for device_id in self.device_json:
            if device_id in result_handler.failed_device_jobs:
                self._logger.debug("Not executing the next operation"
                                   "in the workflow for device: %s"
                                   % device_id)
                continue

            device_data = self.device_json.get(device_id)
            device_fqname = ':'.join(
                map(str, device_data.get('device_fqname')))
            device_name = device_data.get('device_fqname', [""])[-1]

            # update prouter UVE
            job_template_fq_name = ':'.join(
                map(str, self.job_template.fq_name))
            pr_fabric_job_template_fq_name = device_fqname + ":" + \
                self.fabric_fq_name + ":" + \
                job_template_fq_name
            self.job_log_utils.send_prouter_job_uve(
                self.job_template.fq_name,
                pr_fabric_job_template_fq_name,
                self.job_execution_id,
                job_status="IN_PROGRESS")

            job_worker_pool.start(Greenlet(job_handler.handle_job,
                                           result_handler,
                                           job_percent_per_task, device_id,
                                           device_name))
        job_worker_pool.join()

    def handle_single_job(self, job_handler, result_handler):
        job_percent_per_task = self.job_log_utils.calculate_job_percentage(
            1, buffer_task_percent=False, total_percent=self.job_percent)[0]
        job_handler.handle_job(result_handler, job_percent_per_task)


class WFManager(object):

    def __init__(self, logger, vnc_api, job_input, job_log_utils, zk_client):
        """Initializes workflow manager."""
        self._logger = logger
        self._vnc_api = vnc_api
        self.job_input = job_input
        self.job_log_utils = job_log_utils
        self.job_execution_id = None
        self.job_template_id = None
        self.device_json = None
        self.result_handler = None
        self.job_data = None
        self.fabric_fq_name = None
        self.parse_job_input(job_input)
        self.job_utils = JobUtils(self.job_execution_id,
                                  self.job_template_id,
                                  self._logger, self._vnc_api)
        self._zk_client = zk_client
        self.job_mgr = None
        self.job_template = None
        self.abort_flag = False
        signal.signal(signal.SIGABRT,  self.job_mgr_abort_signal_handler)
        signal.signal(signal.SIGUSR1,  self.job_mgr_abort_signal_handler)
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
        self.job_execution_id = job_input_json.get('job_execution_id')
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
            if isinstance(input_schema, basestring):
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
            self.result_handler = JobResultHandler(self.job_template_id,
                                                   self.job_execution_id,
                                                   self.fabric_fq_name,
                                                   self._logger,
                                                   self.job_utils,
                                                   self.job_log_utils)

            job_template = self.job_utils.read_job_template()
            self.job_template = job_template

            msg = MsgBundle.getMessage(
                MsgBundle.START_JOB_MESSAGE,
                job_execution_id=self.job_execution_id,
                job_template_name=job_template.fq_name[-1])
            self._logger.debug(msg)

            timestamp = int(round(time.time() * 1000))
            self.job_log_utils.send_job_log(job_template.fq_name,
                                            self.job_execution_id,
                                            self.fabric_fq_name,
                                            msg,
                                            JobStatus.STARTING.value,
                                            timestamp=timestamp)

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

                # check if its a multi device playbook
                playbooks = job_template.get_job_template_playbooks()
                play_info = playbooks.playbook_info[i]
                multi_device_playbook = play_info.multi_device_playbook

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

                retry_devices = None
                while True:
                    job_mgr = JobManager(self._logger, self._vnc_api,
                                         self.job_input, self.job_log_utils,
                                         job_template,
                                         self.result_handler, self.job_utils,
                                         i, job_percent, self._zk_client)
                    self.job_mgr = job_mgr
                    job_mgr.start_job()

                    # retry the playbook execution if retry_devices is added to
                    # the playbook output
                    job_status = self.result_handler.job_result_status
                    retry_devices = self.result_handler.get_retry_devices()
                    if job_status == JobStatus.FAILURE or not retry_devices \
                            or self.abort_flag:
                        break
                    self.job_input['device_json'] = retry_devices

                # update the job input with marked playbook output json
                pb_output = self.result_handler.playbook_output or {}

                if pb_output.get('early_exit'):
                    break

                # stop the workflow if playbook failed
                if self.result_handler.job_result_status == JobStatus.FAILURE:

                    # stop workflow only if its a single device job or
                    # it is a multi device playbook
                    # and all the devices have failed some job execution
                    # declare it as failure and the stop the workflow

                    if not multi_device_playbook or \
                            (multi_device_playbook and
                             len(self.result_handler.failed_device_jobs) ==
                             len(self.job_input.get('device_json'))):
                        self._logger.error(
                            "Stop the workflow on the failed Playbook.")
                        break

                    elif not retry_devices:
                        # it is a multi device playbook but one of
                        # the device jobs have failed. This means we should
                        # still declare the operation as success. We declare
                        # workflow as success even if one of the devices has
                        # succeeded the job

                        self.result_handler.job_result_status =\
                            JobStatus.SUCCESS

                if self.abort_flag:
                    err_msg = "ABORTING NOW..."
                    self._logger.info(err_msg)
                    self.result_handler.update_job_status(JobStatus.FAILURE, err_msg)
                    break

                # update the job input with marked playbook output json
                pb_output = self.result_handler.playbook_output or {}

                # read the device_data output of the playbook
                # and update the job input so that it can be used in next
                # iteration
                if not multi_device_playbook:
                    device_json = pb_output.pop('device_json', None)
                    self.job_input['device_json'] = device_json

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

    def job_mgr_abort_signal_handler(self, signalnum, frame):
        if signalnum == signal.SIGABRT:
            # Force abort; kill all playbooks, then exit
            err_msg = "Job aborting..."
            self._logger.info(err_msg)
            try:
                self.job_mgr.job_handler.playbook_abort()
                self.result_handler.update_job_status(JobStatus.FAILURE, err_msg)
                self.result_handler.create_job_summary_log(self.job_template.fq_name)
                sys.exit()
            except Exception as ex:
                self._logger.error("Failed to force abort")
        elif signalnum == signal.SIGUSR1:
            # Graceful abort; Exit after current playbook
            self._logger.info("Job will abort upon playbook completion...")
            self.abort_flag = True

def parse_args():
    parser = argparse.ArgumentParser(description='Job manager parameters')
    parser.add_argument('-i', '--job_input', nargs=1,
                        help='Job manager input json')
    return parser.parse_args()


def initialize_vnc_api(auth_token, api_server_host, vnc_api_init_params):
    if auth_token is not None:
        vnc_api = VncApi(auth_token=auth_token)
    elif vnc_api_init_params is not None:
        vnc_api = VncApi(
            vnc_api_init_params.get("admin_user"),
            vnc_api_init_params.get("admin_password"),
            vnc_api_init_params.get("admin_tenant_name"),
            api_server_host,
            vnc_api_init_params.get("api_server_port"),
            api_server_use_ssl=vnc_api_init_params.get("api_server_use_ssl"))
    else:
        vnc_api = VncApi()
    return vnc_api


def initialize_zookeeper_client(args):
    if 'host_ip' in args:
        host_ip = args.host_ip
    else:
        host_ip = socket.gethostbyname(socket.getfqdn())

    if args.cluster_id:
        client_pfx = args.cluster_id + '-'
    else:
        client_pfx = ''

    zookeeper_client = ZookeeperClient(client_pfx + "job-manager",
                                       args.zk_server_ip, host_ip)
    return zookeeper_client


def handle_init_failure(job_input_json, error_num, error_msg):
    logger.error(MsgBundle.getMessage(error_num,
                                      exc_msg=traceback.format_exc()))
    msg = MsgBundle.getMessage(error_num, exc_msg=error_msg)
    job_log_utils.send_job_log(job_input_json.get('job_template_fq_name'),
                               job_input_json.get('job_execution_id'),
                               job_input_json.get('fabric_fq_name'),
                               msg, JobStatus.FAILURE)
    sys.exit(msg)


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
            sandesh_instance_id=job_input_json.get('job_execution_id'),
            config_args=job_input_json.get('args'))
        logger = job_log_utils.config_logger
    except Exception as exp:
        print >> sys.stderr, "Failed to initialize logger due "\
                             "to Exception: %s" % traceback.format_exc()
        sys.exit(
            "Exiting due to logger initialization error: %s" % repr(exp))

    # initialize _vnc_api instance
    vnc_api = None
    try:
        vnc_api = initialize_vnc_api(job_input_json.get('auth_token'),
                                     job_input_json.get('api_server_host'),
                                     job_input_json.get('vnc_api_init_params'))
        logger.info("VNC api is initialized.")
    except Exception as exp:
        handle_init_failure(job_input_json, MsgBundle.VNC_INITIALIZATION_ERROR,
                            repr(exp))

    # initialize zk
    zk_client = None
    try:
        zk_client = initialize_zookeeper_client(logger._args)
        logger.info("Zookeeper client is initialized.")
    except Exception as exp:
        handle_init_failure(job_input_json, MsgBundle.ZK_INIT_FAILURE,
                            repr(exp))

    # invoke job manager
    try:
        workflow_manager = WFManager(logger, vnc_api, job_input_json,
                                     job_log_utils, zk_client)
        logger.info("Job Manager is initialized. Starting job.")
        workflow_manager.start_job()
    except Exception as exp:
        handle_init_failure(job_input_json, MsgBundle.JOB_ERROR,
                            repr(exp))
