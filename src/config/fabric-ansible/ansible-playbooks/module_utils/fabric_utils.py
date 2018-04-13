#!/usr/bin/python


# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains general utility functions for fabric ansible modules
"""

from ansible.module_utils.basic import AnsibleModule
from job_manager.fabric_logger import fabric_ansible_logger

#
# Sub-class to provide additional functionality to custom ansible modules
#
class FabricAnsibleModule(AnsibleModule):
    def __init__(self, argument_spec={}, **kwargs):
        super(
            FabricAnsibleModule,
            self).__init__(
            argument_spec=argument_spec,
            **kwargs)
        self.module_name = self._name
        self.ctx = self.params.get('job_ctx')
        self.logger = fabric_ansible_logger(self.module_name)
        self.logger.debug("Module params: {}".format(self.params))
