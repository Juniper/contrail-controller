#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
# This file contains implementation of creating PROUTER objectlog
# via sandesh
#

DOCUMENTATION = '''
---

module: prouter_objectlog
author: Juniper Networks
short_description: private module to create device object log
description:
    - This module invokes Sandesh API to append device object log in
      Contrail analytics database
requirements:
    - Contrail analytics must be reachable from API server
options:
    job_ctx:
        description:
            - job context passed from job manager
        required: true
    prouter_fqname:
        description:
            - 'name' field of the device object log that must be device fqname
        required: true
    onboarding_state:
        description:
            - 'onboarding_state' field of the device object log to capture
              device onboarding state
        required: true
    os_version:
        description:
            - optional 'os_version' field of the device object log for the
              device running image version
        required: false
    serial_num:
        description:
            - optional 'serial_num' field of the device object log to capture
              device serial number
        required: false
'''

EXAMPLES = '''
    - name: PR objectlog update to set onboarding state
      prouter_objectlog:
        job_ctx: "{{ job_ctx }}"
        prouter_fqname: "{{ prouter_info.fq_name }}"
        onboarding_state: "{{ DEVICE_STATE.UNDERLAY_CONFIGURED }}"
'''

from ansible.module_utils.fabric_utils import FabricAnsibleModule


def process_module(module):
    # Fetch module params
    prouter_fqname = module.params['prouter_fqname']
    os_version = module.params['os_version']
    serial_num = module.params['serial_num']
    onboarding_state = module.params['onboarding_state']

    module.send_prouter_object_log(prouter_fqname,
                                   onboarding_state,
                                   os_version, serial_num)

    module.exit_json(**module.results)


def main():

    module = FabricAnsibleModule(
        argument_spec=dict(
            prouter_fqname=dict(required=True, type=list),
            job_ctx=dict(required=True, type=dict),
            os_version=dict(type=str),
            serial_num=dict(type=str),
            onboarding_state=dict(required=True, type=str),
        ),
        supports_check_mode=True)

    module.execute(process_module)


if __name__ == '__main__':
    main()
