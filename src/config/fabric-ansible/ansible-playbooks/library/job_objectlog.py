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
from ansible.module_utils.basic import AnsibleModule
from ansible.module_utils.sandesh_log_utils import ObjectLogUtil


def main():
    module = AnsibleModule(
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
    job_result = module.params['result']

    results = dict()
    results['failed'] = False

    object_log = None
    try:
        object_log = ObjectLogUtil(job_ctx)
        object_log.send_job_object_log(message, status, job_result)
    except ValueError as ve:
        results['msg'] = str(ve)
        results['failed'] = True
    except Exception as e:
        msg = "Failed to create following job log due to error: %s\n\t \
               job name: %s\n\t \
               job execution id: %s\n\t \
               job status: %s\n\t, \
               log message: %s\n" \
               % (str(e), job_ctx['job_template_fqname'],
                 job_ctx['job_execution_id'], status, message)
        results['msg'] = msg
        results['failed'] = True
    finally:
        if object_log:
            object_log.close_sandesh_conn()

    module.exit_json(**results)


if __name__ == '__main__':
    main()
