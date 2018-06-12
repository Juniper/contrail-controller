#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains functions that invoke the playbook.
"""
import sys
import json
import traceback
import argparse
from collections import namedtuple

from ansible import constants as CONST
verbosity = CONST.DEFAULT_VERBOSITY or 0

# Overrides the default logger from ansible/utils/display.py.
# fabric_ansible_logger customizes log message formatting
# Note that some internal ansible code inherits "display" from __main__,
# which is this file.
# Also note that CONST is from ansible.cfg
#
from job_manager.fabric_logger import fabric_ansible_logger
logger = fabric_ansible_logger("ansible")

import ansible.utils.display as default_display
default_display.logger = logger

from ansible.utils.display import Display
display = Display(verbosity)

from ansible.parsing.dataloader import DataLoader
from ansible.vars.manager import VariableManager
from ansible.inventory.manager import InventoryManager
from ansible.executor.playbook_executor import PlaybookExecutor

from job_manager.job_messages import MsgBundle
from job_manager.job_manager_logger import job_mgr_logger
JM_LOGGER = job_mgr_logger("FabricAnsible")


class PlaybookHelper(object):

    def get_plugin_output(self, pbex):
        output_json = pbex._tqm._variable_manager._nonpersistent_fact_cache[
            'localhost']['output']
        return output_json

    def execute_playbook(self, playbook_info):
        try:
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
                              forks=100, remote_user=None,
                              private_key_file=None, ssh_common_args=None,
                              ssh_extra_args=None, sftp_extra_args=None,
                              scp_extra_args=None, become=None,
                              become_method=None, become_user=None,
                              verbosity=None, check=False, diff=False)

            variable_manager.extra_vars = playbook_info['extra_vars']

            pbex = PlaybookExecutor(playbooks=[playbook_info['uri']],
                                    inventory=inventory,
                                    variable_manager=variable_manager,
                                    loader=loader,
                                    options=options, passwords=None)
            ret_val = pbex.run()
            if ret_val != 0:
                msg = MsgBundle.getMessage(MsgBundle.
                                           PLAYBOOK_RETURN_WITH_ERROR)
                raise Exception(msg)

            output = self.get_plugin_output(pbex)
            if output is None or output.get('status') is None:
                msg = MsgBundle.getMessage(MsgBundle.
                                           PLAYBOOK_OUTPUT_MISSING)
                raise Exception(msg)

            if output.get('status').lower() == "failure":
                msg = MsgBundle.getMessage(MsgBundle.
                                           PLAYBOOK_STATUS_FAILED)
                raise Exception(msg)

            return output
        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.PLAYBOOK_EXECUTE_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       execution_id=playbook_info['extra_vars']
                                       ['playbook_input']['job_execution_id'],
                                       exc_msg=repr(exp))
            if exp.message:
                msg = msg + "\n" + exp.message

            JM_LOGGER.error(msg)
            sys.exit(msg)


def parse_args():
    parser = argparse.ArgumentParser(description='Ansible playbook input '
                                                 'parameters')
    parser.add_argument('-i', '--playbook_input', nargs=1,
                        help='Playbook input json')
    return parser.parse_args()


if __name__ == "__main__":

    playbook_input_json = None
    try:
        playbook_params = parse_args()
        playbook_input_json = json.loads(playbook_params.playbook_input[0])
        if playbook_input_json is None:
            sys.exit(MsgBundle.getMessage(MsgBundle.NO_PLAYBOOK_INPUT_DATA))
    except Exception as exp:
        ERR_MSG = "Failed to start playbook due "\
              "to Exception: %s" % traceback.print_stack()
        JM_LOGGER.error(ERR_MSG)
        sys.exit(MsgBundle.getMessage(MsgBundle.PLAYBOOK_INPUT_PARSING_ERROR,
                                      exc_msg=repr(exp)))
    playbook_helper = PlaybookHelper()
    playbook_helper.execute_playbook(playbook_input_json)

