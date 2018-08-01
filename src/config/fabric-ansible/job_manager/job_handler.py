#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager api which involves playbook interactions
"""

import os
import uuid
import json
import subprocess32
import traceback
import ast
import time
import gevent
import requests
import xml.etree.ElementTree as etree

from job_manager.job_exception import JobException
from job_manager.job_utils import JobStatus
from job_manager.job_messages import MsgBundle

JOB_PROGRESS = 'JOB_PROGRESS##'
PLAYBOOK_OUTPUT = 'PLAYBOOK_OUTPUT##'


class JobHandler(object):

    def __init__(self, logger, vnc_api, job_template, execution_id, input,
                 params, job_utils, device_json, auth_token, job_log_utils,
                 sandesh_args, fabric_fq_name, playbook_timeout, playbook_seq,
                 prev_pb_output):
        self._logger = logger
        self._vnc_api = vnc_api
        self._job_template = job_template
        self._execution_id = execution_id
        self._job_input = input
        self._job_params = params
        self._job_utils = job_utils
        self._device_json = device_json
        self._auth_token = auth_token
        self._job_log_utils = job_log_utils
        self._sandesh_args = sandesh_args
        self._fabric_fq_name = fabric_fq_name
        self._playbook_timeout = playbook_timeout
        self._playbook_seq = playbook_seq
        self._playbook_output = prev_pb_output
    # end __init__

    def handle_job(self, result_handler, job_percent_per_task, device_id=None):
        try:
            msg = "Starting playbook execution for job template %s with " \
                  "execution id %s" % (self._job_template.get_uuid(),
                                       self._execution_id)
            self._logger.debug(msg)

            # get the playbook information from the job template
            playbook_info = self.get_playbook_info(job_percent_per_task,
                                                   device_id)

            # run the playbook
            self.run_playbook(
                playbook_info,
                result_handler.percentage_completed)

            msg = MsgBundle.getMessage(
                MsgBundle.PLAYBOOK_EXECUTION_COMPLETE,
                job_template_name=self._job_template.get_fq_name()[-1],
                job_execution_id=self._execution_id)
            self._logger.debug(msg)
            result_handler.update_job_status(JobStatus.SUCCESS, msg, device_id)
            self.update_result_handler(result_handler)

        except JobException as job_exp:
            self._logger.error("%s" % job_exp.msg)
            self._logger.error("%s" % traceback.format_exc())
            result_handler.update_job_status(JobStatus.FAILURE, job_exp.msg,
                                             device_id)
        except Exception as exp:
            self._logger.error("Error while executing job %s " % repr(exp))
            self._logger.error("%s" % traceback.format_exc())
            result_handler.update_job_status(JobStatus.FAILURE, exp.message,
                                             device_id)
    # end handle_job

    def get_playbook_info(self, job_percent_per_task, device_id=None):
        try:
            # create the cmd line param for the playbook
            extra_vars = {
                'input': self._job_input,
                'prev_pb_output': self._playbook_output,
                'params': self._job_params,
                'job_template_id': self._job_template.get_uuid(),
                'job_template_fqname': self._job_template.fq_name,
                'fabric_fq_name': self._fabric_fq_name,
                'auth_token': self._auth_token,
                'job_execution_id': self._execution_id,
                'args': self._sandesh_args,
                'playbook_job_percentage': job_percent_per_task
            }
            playbooks = self._job_template.get_job_template_playbooks()

            if device_id:
                if not self._device_json:
                    msg = MsgBundle.getMessage(MsgBundle.DEVICE_JSON_NOT_FOUND)
                    raise JobException(msg,
                                       self._execution_id)

                device_data = self._device_json.get(device_id)
                if not device_data:
                    msg = MsgBundle.getMessage(MsgBundle.NO_DEVICE_DATA_FOUND,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                device_family = device_data.get('device_family')
                device_vendor = device_data.get('device_vendor')
                device_product = device_data.get('device_product')

                if not device_vendor or not device_family:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               DEVICE_VENDOR_FAMILY_MISSING,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                if not device_product:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               PRODUCT_NAME_MISSING,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                # check for credentials,required param; else playbooks
                # will fail
                device_username = device_data.get('device_username')
                device_password = device_data.get('device_password')

                if not device_username or not device_password:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               NO_CREDENTIALS_FOUND,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                # update extra-vars to reflect device-related params
                device_fqname = device_data.get('device_fqname')
                device_management_ip = device_data.get('device_management_ip')
                extra_vars.update({
                    'device_id': device_id,
                    'device_fqname': device_fqname,
                    'device_management_ip':
                        device_management_ip,
                    'vendor': device_vendor,
                    'device_family': device_family,
                    'device_username': device_username,
                    'device_password': device_password,
                    'product_name': device_product
                })

                self._logger.debug("Passing the following device "
                                   "ip to playbook %s " % device_management_ip)

            # get the playbook uri from the job template
            play_info = playbooks.playbook_info[self._playbook_seq]

            playbook_input = {'playbook_input': extra_vars}

            playbook_info = dict()
            playbook_info['uri'] = play_info.playbook_uri
            playbook_info['extra_vars'] = playbook_input

            return playbook_info
        except JobException:
            raise
        except Exception as exp:
            msg = MsgBundle.getMessage(
                      MsgBundle.GET_PLAYBOOK_INFO_ERROR,
                      job_template_id=self._job_template.get_uuid(),
                      exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)
    # end get_playbook_info

    def process_file_and_get_marked_output(self, unique_pb_id,
                                           exec_id, playbook_process):
        f_read = None
        marked_output = {}
        markers = [PLAYBOOK_OUTPUT, JOB_PROGRESS]
        # read file as long as it is not a time out
        # Adding a residue timeout of 5 mins in case
        # the playbook dumps all the output towards the
        # end and it takes sometime to process and read it
        file_read_timeout = self._playbook_timeout + 300
        # initialize it to the time when the subprocess
        # was spawned
        last_read_time = time.time()

        while True:
            try:
                f_read = open("/tmp/"+exec_id, "r")
                self._logger.info("File got created .. "
                                  "proceeding to read contents..")
                current_time = time.time()
                while current_time - last_read_time < file_read_timeout:
                    line_read = f_read.readline()
                    if line_read:
                        self._logger.info("Line read ..." + line_read)
                        if unique_pb_id in line_read:
                            total_output =\
                                line_read.split(unique_pb_id)[-1].strip()
                            if total_output == 'END':
                                break
                            for marker in markers:
                                if marker in total_output:
                                    marked_output[marker] = total_output
                                    if marker == JOB_PROGRESS:
                                        marked_jsons =\
                                            self._extract_marked_json(
                                                marked_output)
                                        self._job_progress =\
                                            marked_jsons.get(JOB_PROGRESS)
                                        self.current_percentage += float(
                                            self._job_progress)
                                        self._job_log_utils.\
                                            send_job_execution_uve(
                                                self._fabric_fq_name,
                                                self._job_template.fq_name,
                                                exec_id,
                                                percentage_completed=
                                                self.current_percentage)
                    else:
                        # this sleep is essential
                        # to yield the context to
                        # sandesh uve for % update
                        gevent.sleep(0)

                    current_time = time.time()
                break
            except IOError as file_not_found_err:
                self._logger.info("File not yet created !!")
                # check if the sub-process died but file is not created
                # if yes, it is old-usecase or there were no markers
                if playbook_process.poll() is not None:
                    self._logger.info("No markers found....")
                    break
                # for the case when the sub-process hangs for some
                # reason and does not write to/create file
                elif time.time() - last_read_time > file_read_timeout:
                    self._logger.info("Sub process probably hung; "
                                      "stopping file processing ....")
                    break
                time.sleep(0.5)
            finally:
                if f_read is not None:
                    f_read.close()
                    self._logger.info("File closed successfully!")

        return marked_output
    # end process_file_and_get_marked_output

    def send_pr_object_log(self, exec_id, start_time, end_time, pb_status):
        status = "SUCCESS"
        elapsed_time = end_time - start_time

        if pb_status != 0:
            status = "FAILURE"

        payload = {
            'start_time': 'now-%ds' % (elapsed_time),
            'end_time': 'now',
            'select_fields': ['MessageTS', 'Messagetype', 'ObjectLog'],
            'table': 'ObjectJobExecutionTable',
            'where': [
                [
                    {
                        'name': 'ObjectId',
                        'value': '%s' % exec_id,
                        'op': 1
                    }
                ]
            ]
        }
        url = "http://localhost:8081/analytics/query"

        resp = requests.post(url, json=payload)
        if resp.status_code == 200:
            PRouterOnboardingLog = resp.json().get('value')
            for PRObjectLog in PRouterOnboardingLog:
                resp = PRObjectLog.get('ObjectLog')
                xmlresp = etree.fromstring(resp)
                for ele in xmlresp.iter():
                    if ele.tag == 'name':
                        device_fqname = ele.text
                    if ele.tag == 'onboarding_state':
                        onboarding_state = ele.text
                job_template_fq_name = ':'.join(
                    map(str, self._job_template.fq_name))
                pr_fabric_job_template_fq_name = device_fqname + ":" + \
                    self._fabric_fq_name + ":" + \
                    job_template_fq_name
                self._job_log_utils.send_prouter_job_uve(
                    self._job_template.fq_name,
                    pr_fabric_job_template_fq_name,
                    exec_id,
                    prouter_state=onboarding_state,
                    job_status=status)

    # end send_pr_object_log

    def run_playbook_process(self, playbook_info, percentage_completed):
        playbook_process = None
        self.current_percentage = percentage_completed
        try:
            playbook_exec_path = os.path.dirname(__file__) \
                + "/playbook_helper.py"

            unique_pb_id = str(uuid.uuid4())
            playbook_info['extra_vars']['playbook_input']['unique_pb_id']\
                = unique_pb_id
            exec_id =\
                playbook_info['extra_vars']['playbook_input'][
                    'job_execution_id']

            pr_object_log_start_time = time.time()

            playbook_process = subprocess32.Popen(["python",
                                                   playbook_exec_path,
                                                   "-i",
                                                   json.dumps(playbook_info)],
                                                  close_fds=True, cwd='/')
            # this is to yield the context to the playbooks so that
            # they start running concurrently
            gevent.sleep(0)
            marked_output = self.process_file_and_get_marked_output(
                unique_pb_id, exec_id, playbook_process
            )

            marked_jsons = self._extract_marked_json(marked_output)
            self._playbook_output = marked_jsons.get(PLAYBOOK_OUTPUT)
            playbook_process.wait(timeout=self._playbook_timeout)
            pr_object_log_end_time = time.time()

            self.send_pr_object_log(
                exec_id,
                pr_object_log_start_time,
                pr_object_log_end_time,
                playbook_process.returncode)

        except subprocess32.TimeoutExpired as timeout_exp:
            if playbook_process is not None:
                os.kill(playbook_process.pid, 9)
            msg = MsgBundle.getMessage(
                      MsgBundle.RUN_PLAYBOOK_PROCESS_TIMEOUT,
                      playbook_uri=playbook_info['uri'],
                      exc_msg=repr(timeout_exp))
            raise JobException(msg, self._execution_id)

        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.
                                       RUN_PLAYBOOK_PROCESS_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)

        if playbook_process.returncode != 0:
            msg = MsgBundle.getMessage(MsgBundle.
                                       PLAYBOOK_EXIT_WITH_ERROR,
                                       playbook_uri=playbook_info['uri'])
            raise JobException(msg, self._execution_id)
    # end run_playbook_process

    def run_playbook(self, playbook_info, percentage_completed):
        try:
            # create job log to capture the start of the playbook
            device_name = \
                playbook_info['extra_vars']['playbook_input'].get(
                    'device_fqname')
            if device_name:
                device_name = device_name[-1]
            playbook_name = playbook_info['uri'].split('/')[-1]

            msg = MsgBundle.getMessage(MsgBundle.START_EXE_PB_MSG,
                                       playbook_name=playbook_name)
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             self._fabric_fq_name,
                                             msg, JobStatus.IN_PROGRESS.value,
                                             device_name=device_name)

            if not os.path.exists(playbook_info['uri']):
                msg = MsgBundle.getMessage(MsgBundle.PLAYBOOK_NOT_FOUND,
                                           playbook_uri=playbook_info['uri'])
                raise JobException(msg,
                                   self._execution_id)

            # Run playbook in a separate process. This is needed since
            # ansible cannot be used in a greenlet based patched environment
            self.run_playbook_process(playbook_info, percentage_completed)

            # create job log to capture completion of the playbook execution
            msg = MsgBundle.getMessage(MsgBundle.STOP_EXE_PB_MSG,
                                       playbook_name=playbook_name)
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             self._fabric_fq_name,
                                             msg, JobStatus.IN_PROGRESS.value,
                                             device_name=device_name)
        except JobException:
            raise
        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)
    # end run_playbook

    def update_result_handler(self, result_handler):
        if self._playbook_output:
            result_handler.update_playbook_output(self._playbook_output)

        if self.current_percentage:
            result_handler.percentage_completed = self.current_percentage
    # end update_result_handler

    def _extract_marked_json(self, marked_output):
        retval = {}
        for marker, output in marked_output.iteritems():
            start = output.index(marker) + len(marker)
            end = output.rindex(marker)
            if start > end:
                self._logger.error("Invalid marked output: %s" % output)
                continue
            json_str = output[start:end]
            self._logger.info("Extracted marked output: %s" % json_str)
            try:
                retval[marker] = json.loads(json_str)
            except ValueError as e:
                # assuming failure is due to unicoding in the json string
                retval[marker] = ast.literal_eval(json_str)

        return retval
    # end _extract_marked_json
