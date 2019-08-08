#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Contains functions that invoke the playbook."""

# Overrides the default logger from ansible/utils/display.py.
# fabric_ansible_logger customizes log message formatting
# Note that some internal ansible code inherits "display" from __main__,
# which is this file.
# Also note that CONST is from ansible.cfg
#


import argparse
from builtins import object
from builtins import str
from collections import namedtuple
import errno
import json
import sys
import traceback

from ansible import constants as CONST
from ansible.executor.playbook_executor import PlaybookExecutor
from ansible.inventory.manager import InventoryManager
from ansible.module_utils._text import to_bytes, to_text
from ansible.parsing.dataloader import DataLoader
from ansible.utils.color import stringc
import ansible.utils.display as default_display
from ansible.utils.display import Display
from ansible.vars.manager import VariableManager

from job_manager.fabric_logger import fabric_ansible_logger
from job_manager.job_manager_logger import job_mgr_logger
from job_manager.job_messages import MsgBundle
from job_manager.job_utils import (
    JobFileWrite, PLAYBOOK_EOL_PATTERN
)

verbosity = CONST.DEFAULT_VERBOSITY or 0
logger = fabric_ansible_logger("ansible")


# Overrides the default display processing from ansible/utils/display.py
# The default display method supresses certain output for remote hosts
# while we want this information sent to the logs
def fabric_ansible_display(self, msg, color=None, stderr=False,
                           screen_only=False, log_only=False):
    # Display a message to the user
    # Note: msg *must* be a unicode string to prevent UnicodeError traceback

    nocolor = msg
    if color:
        msg = stringc(msg, color)

    if not log_only:
        if not msg.endswith(u'\n'):
            msg2 = msg + u'\n'
        else:
            msg2 = msg

        msg2 = to_bytes(msg2, encoding=self._output_encoding(stderr=stderr))
        if sys.version_info >= (3,):
            # Convert back to text string on python3
            # We first convert to a byte string so that we get rid of
            # characters that are invalid in the user's locale
            msg2 = to_text(msg2, self._output_encoding(stderr=stderr),
                           errors='replace')

        # Note: After Display() class is refactored need to update the
        # log capture code in 'bin/ansible-connection' (and other
        # relevant places).
        if not stderr:
            fileobj = sys.stdout
        else:
            fileobj = sys.stderr

        fileobj.write(msg2)

        try:
            fileobj.flush()
        except IOError as e:
            # Ignore EPIPE in case fileobj has been prematurely closed, eg.
            # when piping to "head -n1"
            if e.errno != errno.EPIPE:
                raise

    if logger:
        msg2 = nocolor.lstrip(u'\n')

        msg2 = to_bytes(msg2)
        if sys.version_info >= (3,):
            # Convert back to text string on python3
            # We first convert to a byte string so that we get rid of
            # characters that are invalid in the user's locale
            msg2 = to_text(msg2, self._output_encoding(stderr=stderr))

        if color == CONST.COLOR_ERROR:
            logger.error(msg2)
        else:
            logger.info(msg2)


# Use custom display function
default_display.logger = logger
display = Display(verbosity)
JM_LOGGER = job_mgr_logger("FabricAnsible")
default_display.Display.display = fabric_ansible_display


class PlaybookHelper(object):

    def __init__(self):
        """Playbook helper initializer. Creates the playbook log util class."""
        self._job_file_write = JobFileWrite(logger)

    def get_plugin_output(self, pbex):
        output_json = pbex._tqm._variable_manager._nonpersistent_fact_cache[
            'localhost'].get('output')
        return output_json

    def execute_playbook(self, playbook_info):
        output = None
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

            output = self.get_plugin_output(pbex)

            if ret_val != 0:
                msg = MsgBundle.getMessage(MsgBundle.
                                           PLAYBOOK_RETURN_WITH_ERROR)
                raise Exception(msg)

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

            # after handling exception, write an END
            # to stop listening to the file if created
            unique_pb_id = playbook_info['extra_vars'][
                'playbook_input']['unique_pb_id']
            exec_id = playbook_info['extra_vars']['playbook_input'][
                'job_execution_id']
            self._job_file_write.write_to_file(
                exec_id,
                unique_pb_id,
                JobFileWrite.PLAYBOOK_OUTPUT,
                json.dumps(output)
            )
            with open("/tmp/" + exec_id, "a") as f:
                f.write(unique_pb_id + 'END' + PLAYBOOK_EOL_PATTERN)
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
        ERR_MSG = "Failed to start playbook due to Exception: %s" %\
                  traceback.print_stack()
        JM_LOGGER.error(ERR_MSG)
        sys.exit(MsgBundle.getMessage(MsgBundle.PLAYBOOK_INPUT_PARSING_ERROR,
                                      exc_msg=repr(exp)))
    playbook_helper = PlaybookHelper()
    pb_output = playbook_helper.execute_playbook(playbook_input_json)

    # if it comes here, it implies the pb_output is of correct
    # format and is present with status Sucess. So derive the
    # playbook output to be written to file and finally write END to the file

    unique_pb_id = playbook_input_json['extra_vars'][
        'playbook_input']['unique_pb_id']
    exec_id = playbook_input_json['extra_vars']['playbook_input'][
        'job_execution_id']
    try:
        playbook_helper._job_file_write.write_to_file(
            exec_id, unique_pb_id, JobFileWrite.PLAYBOOK_OUTPUT,
            json.dumps(pb_output)
        )
    except Exception as exc:
        ERR_MSG = "Error while trying to parse output"\
                  " from playbook due to exception: %s"\
                  % str(exc)
        JM_LOGGER.error(ERR_MSG)
        # not stopping execution just because of  parsing error
        # no sys.exit therefore
    finally:
        with open("/tmp/" + exec_id, "a") as f:
            f.write(unique_pb_id + 'END' + PLAYBOOK_EOL_PATTERN)
