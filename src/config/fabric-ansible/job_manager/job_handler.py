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

from job_manager.job_exception import JobException
from job_manager.job_utils import JobStatus
from job_manager.job_messages import MsgBundle

JOB_PROGRESS = 'JOB_PROGRESS##'
DEVICE_DATA = 'DEVICEDATA##'
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
        self._device_data = None
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
            self.run_playbook(playbook_info)

            msg = MsgBundle.getMessage(
                      MsgBundle.PLAYBOOK_EXECUTION_COMPLETE,
                      job_template_id=self._job_template.get_uuid(),
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

    def run_playbook_process(self, playbook_info):
        playbook_process = None
        try:
            playbook_exec_path = os.path.dirname(__file__) \
                                 + "/playbook_helper.py"

            unique_pb_id = str(uuid.uuid4())
            playbook_info['extra_vars']['playbook_input']['unique_pb_id']\
                = unique_pb_id
            exec_id =\
                playbook_info['extra_vars']['playbook_input'][
                    'job_execution_id']

            playbook_process = subprocess32.Popen(["python",
                                                   playbook_exec_path,
                                                   "-i",
                                                   json.dumps(playbook_info)],
                                                  close_fds=True, cwd='/')

            marked_output = {}
            markers = [DEVICE_DATA, PLAYBOOK_OUTPUT, JOB_PROGRESS]
            percentage_completed = 0
            while True:
                try:
                    f_read = open("/tmp/"+exec_id, "r")
                    self._logger.info("File got created .. "
                                      "proceeding to read contents..")
                    while True:
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
                                            job_progress =\
                                                marked_jsons.get(JOB_PROGRESS)
                                            percentage_completed +=\
                                                int(job_progress[
                                                        'percent_complete'])
                                            self._job_log_utils.\
                                                send_job_execution_uve(
                                                    self._job_template.fq_name,
                                                    self._execution_id,
                                                    percentage_completed=
                                                    percentage_completed)
                        else:
                            prev_ptr = f_read.tell()
                            f_read.close()
                            time.sleep(0.5)
                            f_read = open("/tmp/"+exec_id, "r")
                            f_read.seek(prev_ptr)
                    f_read.close()
                    break
                except IOError as file_not_found_err:
                    self._logger.info("File not yet created !!")
                    # check if the sub-process died but file is not created
                    # if yes, it is old-usecase or there were no markers
                    if playbook_process.poll() is not None:
                        self._logger.info("No markers found....")
                        break
                    time.sleep(10)

            marked_jsons = self._extract_marked_json(marked_output)
            self._device_data = marked_jsons.get(DEVICE_DATA)
            self._playbook_output = marked_jsons.get(PLAYBOOK_OUTPUT)
            playbook_process.wait(timeout=self._playbook_timeout)

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

    def run_playbook(self, playbook_info):
        try:
            # create job log to capture the start of the playbook
            device_id = \
                playbook_info['extra_vars']['playbook_input'].get(
                    'device_id', "")
            msg = MsgBundle.getMessage(MsgBundle.START_EXECUTE_PLAYBOOK_MSG,
                                       playbook_uri=playbook_info['uri'],
                                       device_id=device_id,
                                       input_params=json.dumps(
                                                    playbook_info['extra_vars']
                                                    ['playbook_input']
                                                    ['input']),
                                       extra_params=json.dumps(
                                                    playbook_info['extra_vars']
                                                    ['playbook_input']
                                                    ['params']))
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             self._fabric_fq_name,
                                             msg, JobStatus.IN_PROGRESS.value)

            if not os.path.exists(playbook_info['uri']):
                msg = MsgBundle.getMessage(MsgBundle.PLAYBOOK_NOT_FOUND,
                                           playbook_uri=playbook_info['uri'])
                raise JobException(msg,
                                   self._execution_id)

            # Run playbook in a separate process. This is needed since
            # ansible cannot be used in a greenlet based patched environment
            self.run_playbook_process(playbook_info)

            # create job log to capture completion of the playbook execution
            msg = MsgBundle.getMessage(MsgBundle.PB_EXEC_COMPLETE_WITH_INFO,
                                       playbook_uri=playbook_info['uri'],
                                       device_id=device_id)
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             self._fabric_fq_name,
                                             msg, JobStatus.IN_PROGRESS.value)
        except JobException:
            raise
        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)
    # end run_playbook

    def update_result_handler(self, result_handler):
        if self._device_data:
            for device_id in self._device_data:
                result_handler.update_device_data(device_id,
                                                  self._device_data[device_id])
        if self._playbook_output:
            result_handler.update_playbook_output(self._playbook_output)
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
            except ValueError, e:
                # assuming failure is due to unicoding in the json string
                retval[marker] = ast.literal_eval(json_str)

        return retval
    # end _extrace_marked_json
