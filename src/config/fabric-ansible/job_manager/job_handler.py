#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager api which involes playbook interactions
"""

import os
import json
from collections import namedtuple
import traceback

from ansible.parsing.dataloader import DataLoader
from ansible.vars.manager import VariableManager
from ansible.inventory.manager import InventoryManager
from ansible.executor.playbook_executor import PlaybookExecutor

from job_utils import JobStatus
from job_exception import JobException


class JobHandler(object):

    def __init__(self, logger, vnc_api, job_template, execution_id, input,
                 params, job_utils, device_json, auth_token):
        self._logger = logger
        self._vnc_api = vnc_api
        self._job_template = job_template
        self._execution_id = execution_id
        self._job_input = input
        self._job_params = params
        self._job_utils = job_utils
        self._device_json = device_json
        self._auth_token = auth_token

    def handle_job(self, result_handler):
        msg = "Starting playbook execution for job template %s with " \
              "execution id %s" % (self._job_template.get_uuid(),
                                   self._execution_id)
        self._logger.debug(msg)

        # get the playbook information from the job template
        playbook_info = self.get_playbook_info()

        # run the playbook
        output = self.run_playbook(playbook_info)
        result_handler.update_job_status(output)

        msg = "Completed playbook execution for job template %s with " \
              "execution id %s" % (self._job_template.get_uuid(),
                                   self._execution_id)
        self._logger.debug(msg)

    def handle_device_job(self, device_id, result_handler):
        msg = "Starting playbook for job template %s with execution " \
              "id %s for device %s " % (self._job_template.get_uuid(),
                                        self._execution_id, device_id)
        self._logger.debug(msg)

        # get the playbook information from the job template
        playbook_info = self.get_playbook_info(device_id)

        # run the playbook
        output = self.run_playbook(playbook_info)

        result_handler.update_job_status(output, device_id)

        msg = "Completed playbook execution for job template %s with " \
              "execution id %s" % (self._job_template.get_uuid(),
                                   self._execution_id)
        self._logger.debug(msg)


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
                "device_vendor or device_family not found for " \
                "%s" % device_id, self._execution_id)

        for playbook_info in playbook_list:
            pb_vendor_name = playbook_info.get_vendor()
            if not pb_vendor_name:
                # device_vendor agnostic
                return playbook_info
            if pb_vendor_name.lower() == device_vendor.lower():
                pb_device_family = playbook_info.get_device_family()
                if pb_device_family:
                    if (device_family.lower() == pb_device_family.lower()):
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
        except Exception as e:
            msg = "Error while getting the playbook information from the " \
                  "job template %s : %s" % (self._job_template.get_uuid(),
                                            repr(e))
            raise JobException(msg, self._execution_id)

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

            if not os.path.exists(playbook_info['uri']):
                raise JobException("Playbook %s does not "
                                   "exist" % playbook_info['uri'],
                                   self._execution_id)

            loader = DataLoader()
            inventory = InventoryManager(loader=loader, sources=['localhost'])
            variable_manager = VariableManager(loader=loader,
                                               inventory=inventory)

            Options = namedtuple('Options',
                                 ['listtags', 'listtasks', 'listhosts',
                                  'syntax', 'connection', 'module_path',
                                  'forks', 'remote_user', 'private_key_file',
                                  'ssh_common_args', 'ssh_extra_args',
                                  'sftp_extra_args', 'scp_extra_args',
                                  'become', 'become_method', 'become_user',
                                  'verbosity', 'check', 'diff'])
            options = Options(listtags=False, listtasks=False, listhosts=False,
                              syntax=False, connection='ssh', module_path=None,
                              forks=100, remote_user='slotlocker',
                              private_key_file=None, ssh_common_args=None,
                              ssh_extra_args=None, sftp_extra_args=None,
                              scp_extra_args=None, become=None,
                              become_method=None, become_user=None,
                              verbosity=None, check=False, diff=False)

            variable_manager.extra_vars = playbook_info['extra_vars']

            passwords = dict(vault_pass='secret')

            pbex = PlaybookExecutor(playbooks=[playbook_info['uri']],
                                    inventory=inventory,
                                    variable_manager=variable_manager,
                                    loader=loader,
                                    options=options, passwords=passwords)

            pbex.run()
            self._logger.debug("Completed executing the playbook %s. "
                               "Collecting output." % playbook_info['uri'])

            output = self.get_plugin_output(pbex)
            self._logger.debug("Output for playbook %s : "
                               "%s" % (playbook_info['uri'],
                                       json.dumps(output)))

            # create job log to capture completion of the playbook execution
            msg = "Completed to execute the playbook %s for" \
                  " device %s" % (playbook_info['uri'],
                                  playbook_info['extra_vars']['playbook_input']
                                  ['device_id'])
            self._logger.debug(msg)

            return output
        except Exception as e:
            msg = "Error while executing the playbook %s : %s" % \
                  (playbook_info['uri'], repr(e))
            raise JobException(msg, self._execution_id)

    def get_plugin_output(self, pbex):
        output_dict = pbex._tqm._variable_manager._nonpersistent_fact_cache[
            'localhost']['output']
        output_json = json.dumps(output_dict)
        return output_json
