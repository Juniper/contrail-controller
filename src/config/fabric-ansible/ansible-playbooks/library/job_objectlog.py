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
from ansible.module_utils.sandesh_log_utils import send_job_object_log


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
    result = module.params['result']

    results = send_job_object_log(job_ctx,
                                  message,
                                  status,
                                  result)

    module.exit_json(**results)


if __name__ == '__main__':
    main()
