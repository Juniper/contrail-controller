#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of passing attributes for prouter
objectlog to callbacks
configuration manager
"""

DOCUMENTATION = '''
---

module: output_adapter
author: Juniper Networks
short_description: Private module to pass attributes for prouter objectlog
to callbacks
description:
    - Pass prouter objectlog attributes to callbacks
requirements:
    -
options:
    name:
        description:
            - Prouter object name to send in prouter objectlog 'name' field
              Eg: fq_name of the object
        required: true
    os_version:
        description:
            - OS version to send in prouter objectlog 'os_version' field
        required: false
    serial_num:
        description:
            - Serial number to send in prouter objectlog 'serial_num' field
        required: false
    onboarding_state:
        description:
            - Onboarding state to send in prouter objectlog 'onboarding_state'
        required: false
'''

EXAMPLES = '''
'''

from ansible.module_utils.basic import *

def main():
    module = AnsibleModule(
                 argument_spec=dict(
                     name=dict(required=True, type=list, ),
                     os_version=dict(required=False, type=str),
                     serial_num=dict(required=False, type=str),
                     onboarding_state=dict(required=False, type=str)
                 ),
                 supports_check_mode=True)

    # Construct results to pass to callback
    results = {}
    results['prouter_object_name'] = ":".join(module.params['name'])
    results['prouter_os_version'] = module.params.get('os_version')
    results['prouter_serial_num'] = module.params.get('serial_num')
    results['prouter_onboarding_state'] = module.params.get('onboarding_state')

    module.exit_json(**results)

if __name__ == '__main__':
    main()
