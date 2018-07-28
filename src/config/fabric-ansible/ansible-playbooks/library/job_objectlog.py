#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of creating JOB objectlogs
via sandesh
"""

DOCUMENTATION = '''
---

module: job_objectlog
author: Juniper Networks
short_description: private module to create job object log
description:
    - This module invokes Sandesh API to send job object log to Contrail
      analytics
requirements:
    - Contrail analytics must be reachable from API server
options:
    job_ctx:
        description:
            - job context passed from job manager
        required: true
    message:
        description:
            - 'message' field in the job object log
        required: true
    status:
        description:
            - 'status' field in the job object log
        required: true
    result:
        description:
            - 'result' field to capture the job result if job is completed with success
        required: false
'''


EXAMPLES = '''
    - name: Appending job log on failure to generate common config
      job_objectlog:
        job_ctx: "{{ job_ctx }}"
        message: "Failed to generate configuration 'common_config':\n{{ cmd_res.msg }}"
        status: "{{ JOBLOG_STATUS.IN_PROCESS }}"
'''

from ansible.module_utils.fabric_utils import FabricAnsibleModule


def process_module(module):
    # Fetch module params
    message = module.params['message']
    status = module.params['status']
    job_result = module.params['result']
    device_name = module.params['device_name']

    module.send_job_object_log(message, status, job_result,
                               device_name=device_name)

    module.exit_json(**module.results)


def main():
    module = FabricAnsibleModule(
        argument_spec=dict(
            job_ctx=dict(required=True, type=dict),
            message=dict(required=True, type=str),
            status=dict(required=True, type=str),
            result=dict(type=dict),
            device_name=dict(type=str)
        ),
        supports_check_mode=True)

    module.execute(process_module)


if __name__ == '__main__':
    main()
