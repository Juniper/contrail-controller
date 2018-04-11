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

import logging
from ansible.module_utils.fabric_utils import FabricAnsibleModule
from ansible.module_utils.sandesh_log_utils import send_job_object_log


def main():
    module = FabricAnsibleModule(
        argument_spec=dict(
            job_ctx=dict(required=True, type=dict),
            message=dict(required=True, type=str),
            status=dict(required=True, type=int),
            result=dict(type=dict),
        ),
        supports_check_mode=True)

    # Fetch module params
    job_ctx = module.params['job_ctx']
    message = module.params['message']
    status = module.params['status']
    result = module.params['result']

    results = send_job_object_log(job_ctx,
                                  message,
                                  status,
                                  result)

    module.exit_json(**results)


if __name__ == '__main__':
    main()
