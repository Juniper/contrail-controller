#!/usr/bin/env python

# Note: Following lines should be in ansible.cfg:
#   host_key_checking = False
#   callback_whitelist = juniper_junos_facts
#
# This line should be in ansible.cfg if we want to specify a directory for callbacks
#   callback_plugins = /Users/josephw/glacier/callbacks
#
# This line should be in ansible.cfg if we want to override stdout
#   stdout_callback = juniper_junos_facts
#

import os
import sys
import json
from collections import namedtuple

from ansible.parsing.dataloader import DataLoader
from ansible.vars.manager import VariableManager
from ansible.inventory.manager import InventoryManager
from ansible.executor.playbook_executor import PlaybookExecutor

playbook_path = '/Users/josephw/glacier/juniper_junos_facts.yaml'
playbook_callback_name = 'juniper_junos_facts'

loader = DataLoader()
inventory = InventoryManager(loader=loader, sources=['localhost'])
variable_manager = VariableManager(loader=loader, inventory=inventory)

if not os.path.exists(playbook_path):
    print '[INFO] The playbook does not exist'
    sys.exit()

Options = namedtuple('Options', ['listtags', 'listtasks', 'listhosts', 'syntax', 'connection','module_path', 'forks', 'remote_user', 'private_key_file', 'ssh_common_args', 'ssh_extra_args', 'sftp_extra_args', 'scp_extra_args', 'become', 'become_method', 'become_user', 'verbosity', 'check', 'diff'])
options = Options(listtags=False, listtasks=False, listhosts=False, syntax=False, connection='ssh', module_path=None, forks=100, remote_user='slotlocker', private_key_file=None, ssh_common_args=None, ssh_extra_args=None, sftp_extra_args=None, scp_extra_args=None, become=None, become_method=None, become_user=None, verbosity=None, check=False, diff=False)

variable_manager.extra_vars = {'foo': 'bar'} # This can accomodate various other command line arguments.`

passwords = dict(vault_pass='secret')

pbex = PlaybookExecutor(playbooks=[playbook_path], inventory=inventory, variable_manager=variable_manager, loader=loader, options=options, passwords=passwords)

results = pbex.run()

# DEBUG: Print results. Callback can be overriding stdout, or is a custom callback
if pbex._tqm._stdout_callback.CALLBACK_NAME == playbook_callback_name:
    print "STDOUT PLUGIN OUTPUT:"
    print json.dumps(pbex._tqm._stdout_callback.results, sort_keys=True, indent=4, separators=(',', ': '))
else:
    for plugin in pbex._tqm._callback_plugins:
        if plugin.CALLBACK_NAME == playbook_callback_name:
            print "CUSTOM PLUGIN OUTPUT:"
            print json.dumps(plugin.results, sort_keys=True, indent=4, separators=(',', ': '))


