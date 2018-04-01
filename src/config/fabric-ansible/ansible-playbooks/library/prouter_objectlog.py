#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of creating PROUTER objectlog
via sandesh
"""

import logging
import json
import uuid
import time
from ansible.module_utils.basic import AnsibleModule
from job_manager.job_log_utils import JobLogUtils


def send_prouter_object_log(module):
    results = {}
    results['failed'] = False

    # Fetch module params
    prouter_name = module.params['prouter_name']
    config_args = module.params['config_args']
    job_execution_id = module.params['job_execution_id']
    job_template_fqname = module.params['job_template_fqname']
    job_input = module.params['job_input']
    os_version = module.params['os_version']
    serial_num = module.params['serial_num']
    onboarding_state = module.params['onboarding_state']

    try:
        job_log_util = JobLogUtils(sandesh_instance_id=str(uuid.uuid4()),
                                   config_args=json.dumps(config_args))
        job_log_util.send_prouter_object_log(
            ":".join(prouter_name),
            job_execution_id,
            json.dumps(job_input),
            job_template_fqname,
            os_version,
            serial_num,
            onboarding_state)
        time.sleep(10)
    except Exception as ex:
        results['msg'] = str(ex)
        results['failed'] = True

    return results


def main():
    module = AnsibleModule(
        argument_spec=dict(
            prouter_name=dict(required=True, type=list),
            config_args=dict(required=True, type=dict),
            job_execution_id=dict(required=True, type=str),
            os_version=dict(type=str),
            serial_num=dict(type=str),
            onboarding_state=dict(type=str),
            job_template_fqname=dict(type=list),
            job_input=dicttype=dict),
        ),
        supports_check_mode=True)

    results = send_prouter_object_log(module)

    module.exit_json(**results)

if __name__ == '__main__':
    main()
