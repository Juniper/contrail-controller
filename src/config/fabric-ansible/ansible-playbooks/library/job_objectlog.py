#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of creating JOB objectlogs
via sandesh
"""

import logging
import json
import uuid
import ast
import time
from ansible.module_utils.basic import AnsibleModule
from job_manager.job_log_utils import JobLogUtils


def send_job_object_log(module):
    results = {}
    results['failed'] = False

    # Fetch module params
    config_args = module.params['config_args']
    job_execution_id = module.params['job_execution_id']
    job_template_fqname = module.params['job_template_fqname']
    message = module.params['message']
    status = module.params['status']
    result = module.params['result']

    try:
        job_log_util = JobLogUtils(sandesh_instance_id=str(uuid.uuid4()),
                                   config_args=json.dumps(config_args))
        job_log_util.send_job_log(
            job_template_fqname,
            job_execution_id,
            message,
            status,
            result)
        time.sleep(10)
    except Exception as ex:
        results['msg'] = str(ex)
        results['failed'] = True

    return results


def main():
    module = AnsibleModule(
        argument_spec=dict(
            job_template_fqname=dict(required=True, type=list),
            config_args=dict(required=True, type=dict),
            job_execution_id=dict(required=True, type=str),
            message=dict(required=True, type=str),
            status=dict(required=True, type=int),
            result=dict(type=dict),
        ),
        supports_check_mode=True)

    results = send_job_object_log(module)

    module.exit_json(**results)

if __name__ == '__main__':
    main()
