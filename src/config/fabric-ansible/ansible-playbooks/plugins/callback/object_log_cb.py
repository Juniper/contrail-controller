#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation ifor callback used to send sandesh
prouter and job objectlog messages
"""

from ansible.plugins.callback import CallbackBase

class CallbackModule(CallbackBase):
    CALLBACK_VERSION = 2.0
    CALLBACK_TYPE = 'notification'
    CALLBACK_NAME = 'objectlog_cb'
    CALLBACK_NEEDS_WHITELIST = True

    def __init__(self, display=None):
        super(CallbackModule, self).__init__(display)

    def v2_runner_on_ok(self, res):

        if 'prouter_object_name' in res._result.keys():
            # todo: call job manager's method
            print res._result['prouter_object_name']
            print res._result.get('prouter_os_version')
            print res._result.get('prouter_serial_num')
            print res._result.get('prouter_onboarding_state')

        if 'job_log_message' in res._result.keys():
            # todo: call job manager's method
            print res._result['job_log_message']
            print res._result.get('job_log_status') or "in-progress"
            print res._result.get('job_log_result') or None
