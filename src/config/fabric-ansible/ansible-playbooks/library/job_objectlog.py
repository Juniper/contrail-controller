#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of creating JOB objectlogs
via sandesh
"""

import logging
from ansible.module_utils.basic import AnsibleModule
from module_utils.sandesh_log_utils import send_job_object_log

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

    # Fetch module params
    config_args = module.params['config_args']
    job_execution_id = module.params['job_execution_id']
    job_template_fqname = module.params['job_template_fqname']
    message = module.params['message']
    status = module.params['status']
    result = module.params['result']

    results = send_job_object_log(config_args,
                                  job_execution_id,
                                  job_template_fqname,
                                  message,
                                  status,
                                  result)

    module.exit_json(**results)


if __name__ == '__main__':
    main()
