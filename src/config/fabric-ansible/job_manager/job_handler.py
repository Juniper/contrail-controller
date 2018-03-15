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
        self.job_log_utils = job_log_utils
        self.sandesh_args = sandesh_args

    def handle_job(self, result_handler):
        try:
            msg = "Starting playbook execution for job template %s with " \
                  "execution id %s" % (self._job_template.get_uuid(),
                                       self._execution_id)
            self._logger.debug(msg)

            # get the playbook information from the job template
            playbook_info = self.get_playbook_info()

            # run the playbook
            self.run_playbook(playbook_info)

            msg = "Completed playbook execution for job template %s with " \
                  "execution id %s" % (self._job_template.get_uuid(),
                                       self._execution_id)
            self._logger.debug(msg)
            result_handler.update_job_status(JobStatus.SUCCESS, msg)
        except JobException as e:
            self._logger.error("%s" % e.msg)
            self._logger.error("%s" % traceback.print_stack())
            result_handler.update_job_status(JobStatus.FAILURE, e.msg)
        except Exception as e:
            self._logger.error("Error while executing job %s " % repr(e))
            self._logger.error("%s" % traceback.print_stack())
            result_handler.update_job_status(JobStatus.FAILURE, e.message)

    def handle_device_job(self, device_id, result_handler):
        try:
            msg = "Starting playbook for job template %s with execution " \
                  "id %s for device %s " % (self._job_template.get_uuid(),
                                            self._execution_id, device_id)
            self._logger.debug(msg)

            # get the playbook information from the job template
            playbook_info = self.get_playbook_info(device_id)

            # run the playbook
            self.run_playbook(playbook_info)

            msg = "Completed playbook execution for job template %s with " \
                  "execution id %s for device %s" % \
                  (self._job_template.get_uuid(), self._execution_id,
                   device_id)
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

    def find_playbook_info(self, device_id, playbook_list):

        device_details = self._device_json.get(device_id)
        if not device_details:
            msg = "Device details for the device %s not found" \
                  % device_id
            raise JobException(msg, self._execution_id)
        device_vendor = device_details.get('device_vendor')
        device_family = device_details.get('device_family')

        if not device_vendor or not device_family:
            raise JobException(
                "device_vendor or device_family not found for %s"
                % device_id, self._execution_id)

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
        msg = "Playbook info not found in the job template for " \
              "%s and %s" % (device_vendor, device_family)
        raise JobException(msg, self._execution_id)

    def get_playbook_info(self, device_id=None):
        try:
            # get the playbook uri from the job template
            device_family = None
            if device_id:
                playbooks = self._job_template.get_job_template_playbooks()
                play_info = self.find_playbook_info(device_id,
                                                    playbooks.playbook_info)
            else:
                playbooks = self._job_template.get_job_template_playbooks()
                play_info = playbooks.playbook_info[0]

            # create the cmd line param for the playbook
            extra_vars = dict()
            extra_vars.update({'input': self._job_input})
            extra_vars.update({'params': self._job_params})
            extra_vars.update(
                {'job_template_id': self._job_template.get_uuid()})
            extra_vars.update({'device_id': device_id})
            extra_vars.update({'device_family': device_family})
            extra_vars.update({'auth_token': self._auth_token})
            extra_vars.update({'job_execution_id': self._execution_id})
            extra_vars.update({'vendor': play_info.vendor})
            extra_vars.update({'args': self.sandesh_args})
            if self._device_json is not None:
                device_data = self._device_json.get(device_id)
                if device_data is not None:
                    extra_vars.update({
                        'device_management_ip':
                            device_data.get('device_management_ip')})
                    extra_vars.update({
                        'device_username': device_data.get('device_username')})
                    extra_vars.update({
                        'device_password': device_data.get('device_password')})
                    extra_vars.update({
                        'device_fqname': device_data.get('device_fqname')})
                    self._logger.debug("Passing the following device "
                                       "ip to playbook %s " % device_data.get(
                                        'device_management_ip'))
            playbook_input = {'playbook_input': extra_vars}

            playbook_info = dict()
            playbook_info.update({'uri': play_info.playbook_uri})
            playbook_info.update({'extra_vars': playbook_input})

            return playbook_info
        except JobException:
            raise
        except Exception as e:
            msg = "Error while getting the playbook information from the " \
                  "job template %s : %s" % (self._job_template.get_uuid(),
                                            repr(e))
            raise JobException(msg, self._execution_id)

    def run_playbook_process(self, playbook_info):
        try:
            playbook_exec_path = os.path.dirname(__file__) \
                                 + "/playbook_helper.py"
            p = subprocess.Popen(["python", playbook_exec_path, "-i",
                                  json.dumps(playbook_info)],
                                 close_fds=True, cwd='/')
            p.wait()
            if p.returncode is not None and p.returncode == 1:
                msg = "Playbook exited with error."
                raise JobException(msg, self._execution_id)
            elif p.returncode is None:
                msg = "No result code got from the playbook"
                raise JobException(msg, self._execution_id)

        except JobException as e:
            raise
        except Exception as e:
            msg = "Exception in creating a playbook process " \
                  "for %s " % playbook_info['uri']
            raise JobException(msg, self._execution_id)
        return

    def run_playbook(self, playbook_info):
        try:
            # create job log to capture the start of the playbook
            device_id = playbook_info['extra_vars']['playbook_input']
            ['device_id']
            if device_id is None:
                device_id = ""
            msg = "Starting to execute the playbook %s for device " \
                  "%s with input %s and params %s " % \
                  (playbook_info['uri'], device_id,
                   json.dumps(playbook_info['extra_vars']['playbook_input']
                              ['input']),
                   json.dumps(playbook_info['extra_vars']['playbook_input']
                              ['params']))
            self._logger.debug(msg)
            self.job_log_utils.send_job_log(self._job_template.get_uuid(),
                                            self._execution_id,
                                            msg, JobStatus.IN_PROGRESS.value)

            if not os.path.exists(playbook_info['uri']):
                raise JobException("Playbook %s does not "
                                   "exist" % playbook_info['uri'],
                                   self._execution_id)

            # Run playbook in a separate process. This is needed since
            # ansible cannot be used in a greenlet based patched environment
            self.run_playbook_process(playbook_info)

            # create job log to capture completion of the playbook execution
            msg = "Completed to execute the playbook %s for" \
                  " device %s" % (playbook_info['uri'],
                                  playbook_info['extra_vars']['playbook_input']
                                  ['device_id'])
            self._logger.debug(msg)
            self.job_log_utils.send_job_log(self._job_template.get_uuid(),
                                            self._execution_id,
                                            msg, JobStatus.IN_PROGRESS.value)
            return
        except JobException:
            raise
        except Exception as e:
            msg = "Error while executing the playbook %s : %s" % \
                  (playbook_info['uri'], repr(e))
            raise JobException(msg, self._execution_id)

