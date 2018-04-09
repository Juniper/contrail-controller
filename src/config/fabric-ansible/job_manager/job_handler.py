#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager api which involves playbook interactions
"""

import os
import json
import subprocess
import traceback

from job_exception import JobException
from job_utils import JobStatus
from job_error_messages import PLAYBOOK_EXIT_WITH_ERROR, \
    DEVICE_JSON_NOT_FOUND, get_no_device_data_error_message, \
    get_missing_credentials_error_message, \
    get_no_dev_fmly_no_vendor_error_message, \
    get_playbook_info_error_message, \
    get_pb_not_found_message, \
    get_playbook_info_device_mismatch_message, \
    get_run_playbook_process_error_message, \
    get_run_playbook_error_message


class JobHandler(object):

    def __init__(self, logger, vnc_api, job_template, execution_id, input,
                 params, job_utils, device_json, auth_token, job_log_utils,
                 sandesh_args):
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

            msg = "Completed playbook execution for job template %s with " \
                  "execution id %s" % (self._job_template.get_uuid(),
                                       self._execution_id)
            self._logger.debug(msg)
            result_handler.update_job_status(JobStatus.SUCCESS, msg, device_id)
        except JobException as e:
            self._logger.error("%s" % e.msg)
            self._logger.error("%s" % traceback.print_stack())
            result_handler.update_job_status(JobStatus.FAILURE, e.msg,
                                             device_id)
        except Exception as e:
            self._logger.error("Error while executing job %s " % repr(e))
            self._logger.error("%s" % traceback.print_stack())
            result_handler.update_job_status(JobStatus.FAILURE, e.message,
                                             device_id)

    def find_playbook_info(self, device_family, device_vendor, playbook_list):

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
        msg = get_playbook_info_device_mismatch_message(
              device_vendor, device_family)
        raise JobException(msg, self._execution_id)

    def get_playbook_info(self, device_id=None):
        try:
            # create the cmd line param for the playbook
            extra_vars = {'input': self._job_input,
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
                    raise JobException(DEVICE_JSON_NOT_FOUND,
                                       self._execution_id)

                device_data = self._device_json.get(device_id)
                if not device_data:
                    raise JobException(get_no_device_data_error_message(
                                       device_id), self._execution_id)

                device_family = device_data.get('device_family')
                device_vendor = device_data.get('device_vendor')

                if not device_vendor or not device_family:
                    msg = get_no_dev_fmly_no_vendor_error_message(
                          device_id)
                    raise JobException(msg, self._execution_id)

                # check for credentials,required param; else playbooks
                # will fail
                device_username = device_data.get('device_username')
                device_password = device_data.get('device_password')

                if not device_username or not device_password:
                    raise JobException(get_missing_credentials_error_message(
                                       device_id), self._execution_id)

                # update extra-vars to reflect device-related params
                device_fqname = device_data.get('device_fqname')
                device_management_ip = device_data.get('device_management_ip')
                extra_vars.update(
                                  {
                                   'device_id': device_id,
                                   'device_fqname': device_fqname,
                                   'device_management_ip':
                                   device_management_ip,
                                   'vendor': device_vendor,
                                   'device_family': device_family,
                                   'device_username': device_username,
                                   'device_password': device_password
                                  }
                                 )

                self._logger.debug("Passing the following device "
                                   "ip to playbook %s " % device_management_ip)

                # get the playbook uri from the job template
                play_info = self.find_playbook_info(device_family,
                                                    device_vendor,
                                                    playbooks.playbook_info)
            else:
                # get the playbook uri from the job template
                play_info = playbooks.playbook_info[0]

            playbook_input = {'playbook_input': extra_vars}

            playbook_info = dict()
            playbook_info['uri'] = play_info.playbook_uri
            playbook_info['extra_vars'] = playbook_input

            return playbook_info
        except JobException:
            raise
        except Exception as e:
            msg = get_playbook_info_error_message(
                  self._job_template.get_uuid(), e)
            raise JobException(msg, self._execution_id)

    def run_playbook_process(self, playbook_info):
        p = None
        try:
            playbook_exec_path = os.path.dirname(__file__) \
                                 + "/playbook_helper.py"
            p = subprocess.Popen(["python", playbook_exec_path, "-i",
                                  json.dumps(playbook_info)],
                                 close_fds=True, cwd='/')
            p.wait()
            if p.returncode != 0:
                raise JobException(PLAYBOOK_EXIT_WITH_ERROR,
                                   self._execution_id)
        except JobException:
            raise
        except Exception as e:
            msg = get_run_playbook_process_error_message(
                  playbook_info['uri'], e)
            raise JobException(msg, self._execution_id)
        return

    def run_playbook(self, playbook_info):
        try:
            # create job log to capture the start of the playbook
            device_id = \
                playbook_info['extra_vars']['playbook_input'].get(
                    'device_id', "")
            msg = "Starting to execute the playbook %s for device " \
                  "%s with input %s and params %s " % \
                  (playbook_info['uri'], device_id,
                   json.dumps(playbook_info['extra_vars']['playbook_input']
                              ['input']),
                   json.dumps(playbook_info['extra_vars']['playbook_input']
                              ['params']))
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             msg, JobStatus.IN_PROGRESS.value)

            if not os.path.exists(playbook_info['uri']):
                raise JobException(get_pb_not_found_message(
                                   playbook_info['uri']),
                                   self._execution_id)

            # Run playbook in a separate process. This is needed since
            # ansible cannot be used in a greenlet based patched environment
            self.run_playbook_process(playbook_info)

            # create job log to capture completion of the playbook execution
            msg = "Completed to execute the playbook %s for" \
                  " device %s" % (playbook_info['uri'], device_id)
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             msg, JobStatus.IN_PROGRESS.value)
        except JobException:
            raise
        except Exception as e:
            msg = get_run_playbook_error_message(playbook_info['uri'], e)
            raise JobException(msg, self._execution_id)
