#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of passing messages and results to callbacks
configuration manager
"""
from ansible.module_utils.basic import AnsibleModule

DOCUMENTATION = '''
---

module: output_adapter
author: Juniper Networks
short_description: Private module to pass messages and results to callbacks
description:
    - Pass message and results to callbacks
requirements:
    -
options:
    message:
        description:
            - String to send in job objectlog 'message' field
        required: true
    status:
        description:
            - String to send in job objectlog 'status' field
        required: true
    result:
        description:
            - JSON formatted string to send in job objectlog 'result' field
        required: true
'''


EXAMPLES = '''
'''


def main():
    module = AnsibleModule(
        argument_spec=dict(
            message=dict(required=True),
            status=dict(required=False),
            result=dict(required=False)
        ),
        supports_check_mode=False)

    results = {}
    results['job_log_message'] = module.params.get('message')
    results['job_log_status'] = module.params.get('status')
    results['job_log_result'] = module.params.get('result')

    module.exit_json(**results)


if __name__ == '__main__':
    main()
