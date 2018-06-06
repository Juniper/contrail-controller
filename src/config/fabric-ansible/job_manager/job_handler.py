#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager api which involves playbook interactions
"""

import os
import json
import subprocess32
import traceback
import ast

from job_exception import JobException
from job_utils import JobStatus
from job_messages import MsgBundle


class JobHandler(object):

    def __init__(self, logger, vnc_api, job_template, execution_id, input,
                 params, job_utils, device_json, auth_token, job_log_utils,
                 sandesh_args, playbook_timeout, playbook_seq=None):
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
        self._playbook_timeout = playbook_timeout
        self._playbook_seq = playbook_seq
        self._device_data = None

    def handle_job(self, result_handler, device_id=None):
        try:
            msg = "Starting playbook execution for job template %s with " \
                  "execution id %s" % (self._job_template.get_uuid(),
                                       self._execution_id)
            self._logger.debug(msg)

            # get the playbook information from the job template
            playbook_info = self.get_playbook_info(device_id)

            # run the playbook
            self.run_playbook(playbook_info)

            msg = MsgBundle.getMessage(
                      MsgBundle.PLAYBOOK_EXECUTION_COMPLETE,
                      job_template_id=self._job_template.get_uuid(),
                      job_execution_id=self._execution_id)
            self._logger.debug(msg)
            result_handler.update_job_status(JobStatus.SUCCESS, msg, device_id)

            self.update_result_handler_device_data(result_handler)

        except JobException as e:
            self._logger.error("%s" % e.msg)
            self._logger.error("%s" % traceback.format_exc())
            result_handler.update_job_status(JobStatus.FAILURE, e.msg,
                                             device_id)
        except Exception as e:
            self._logger.error("Error while executing job %s " % repr(e))
            self._logger.error("%s" % traceback.format_exc())
            result_handler.update_job_status(JobStatus.FAILURE, e.message,
                                             device_id)

    def find_playbook_info(self, device_family, device_vendor, playbook_list):
        if self._playbook_seq:
            return playbook_list[self._playbook_seq]

        for playbook_info in playbook_list:
            pb_vendor_name = playbook_info.get_vendor()
            if not pb_vendor_name:
                # device_vendor agnostic
                return playbook_info
            if pb_vendor_name.lower() == device_vendor.lower():
                pb_device_family = playbook_info.get_device_family()
                if pb_device_family:
                    if device_family.lower() == pb_device_family.lower():
                        return playbook_info
                else:
                    # device_family agnostic
                    return playbook_info
        msg = MsgBundle.getMessage(MsgBundle.
                                   PLAYBOOK_INFO_DEVICE_MISMATCH,
                                   device_vendor=device_vendor,
                                   device_family=device_family)
        raise JobException(msg, self._execution_id)

    def get_playbook_info(self, device_id=None):
        try:
            # create the cmd line param for the playbook
            extra_vars = {
                'input': self._job_input,
                'params': self._job_params,
                'job_template_id': self._job_template.get_uuid(),
                'job_template_fqname': self._job_template.fq_name,
                'auth_token': self._auth_token,
                'job_execution_id': self._execution_id,
                'args': self._sandesh_args
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

                if not device_vendor or not device_family:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               DEVICE_VENDOR_FAMILY_MISSING,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                # check for credentials,required param; else playbooks
                # will fail
                import pdb; pdb.set_trace()
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
                    'device_password': device_password
                })

                self._logger.debug("Passing the following device "
                                   "ip to playbook %s " % device_management_ip)

                # get the playbook uri from the job template
                play_info = self.find_playbook_info(device_family,
                                                    device_vendor,
                                                    playbooks.playbook_info)
            else:
                # get the playbook uri from the job template
                play_info = playbooks.playbook_info[self._playbook_seq]

            playbook_input = {'playbook_input': extra_vars}

            playbook_info = dict()
            playbook_info['uri'] = play_info.playbook_uri
            playbook_info['extra_vars'] = playbook_input

            return playbook_info
        except JobException:
            raise
        except Exception as e:
            msg = MsgBundle.getMessage(
                      MsgBundle.GET_PLAYBOOK_INFO_ERROR,
                      job_template_id=self._job_template.get_uuid(),
                      exc_msg=repr(e))
            raise JobException(msg, self._execution_id)

    def run_playbook_process(self, playbook_info):
        p = None
        try:
            playbook_exec_path = os.path.dirname(__file__) \
                                 + "/playbook_helper.py"
            p = subprocess32.Popen(["python", playbook_exec_path, "-i",
                                    json.dumps(playbook_info)],
                                   close_fds=True, cwd='/',
                                   stdout=subprocess32.PIPE)
            device_data_output = ""
            device_data_keyword = 'DEVICEDATA##'
            while True:
                output = p.stdout.readline()
                if output == '' and p.poll() is not None:
                    break
                if output:
                    self._logger.error("------SIRISHA---" + str(output))
                    # read device list data and store in variable result handler
                    if device_data_keyword in output:
                        device_data_output = output

            if device_data_output != "":
                #strip of the data before and after from the device_data_output
                device_data_output = device_data_output[device_data_output.index
                                                        (device_data_keyword) +
                                                        len(device_data_keyword):
                                                        device_data_output.
                                                        rindex(device_data_keyword)]
                self._logger.error("------device_data---" + str(device_data_output))
                try:
                    self._device_data = json.loads(device_data_output)
                except ValueError, e:
                    self._device_data = ast.literal_eval(device_data_output)
                self._logger.error("------json device data---" + str(self._device_data))

            p.wait(timeout=self._playbook_timeout)

        except subprocess32.TimeoutExpired as e:
            if p is not None:
                os.kill(p.pid, 9)
            msg = MsgBundle.getMessage(
                      MsgBundle.RUN_PLAYBOOK_PROCESS_TIMEOUT,
                      playbook_uri=playbook_info['uri'],
                      exc_msg=repr(e))
            raise JobException(msg, self._execution_id)
        except Exception as e:
            msg = MsgBundle.getMessage(MsgBundle.
                                       RUN_PLAYBOOK_PROCESS_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(e))
            raise JobException(msg, self._execution_id)

        if p.returncode != 0:
            msg = MsgBundle.getMessage(MsgBundle.
                                       PLAYBOOK_EXIT_WITH_ERROR,
                                       playbook_uri=playbook_info['uri'])
            raise JobException(msg, self._execution_id)

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
                                             msg, JobStatus.IN_PROGRESS.value)
        except JobException:
            raise
        except Exception as e:
            msg = MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(e))
            raise JobException(msg, self._execution_id)

    def update_result_handler_device_data(self, result_handler):
        if self._device_data:
            for device_id in self._device_data:
                result_handler.update_device_data(device_id, self._device_data[device_id])

