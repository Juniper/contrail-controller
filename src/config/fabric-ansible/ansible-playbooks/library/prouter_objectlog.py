#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of creating PROUTER objectlog
via sandesh
"""

import logging
from ansible.module_utils.basic import AnsibleModule
from ansible.module_utils.sandesh_log_utils import send_prouter_object_log


def main():
    module = AnsibleModule(
        argument_spec=dict(
            prouter_fqname=dict(required=True, type=list),
            config_args=dict(required=True, type=dict),
            job_execution_id=dict(required=True, type=str),
            os_version=dict(type=str),
            serial_num=dict(type=str),
            onboarding_state=dict(required=True, type=str),
            job_template_fqname=dict(required=True, type=list),
            job_input=dict(required=True, type=dict),
        ),
        supports_check_mode=True)

    # Fetch module params
    prouter_fqname = module.params['prouter_fqname']
    config_args = module.params['config_args']
    job_execution_id = module.params['job_execution_id']
    job_template_fqname = module.params['job_template_fqname']
    job_input = module.params['job_input']
    os_version = module.params['os_version']
    serial_num = module.params['serial_num']
    onboarding_state = module.params['onboarding_state']

    results = send_prouter_object_log(prouter_fqname,
                                      config_args,
                                      job_execution_id,
                                      job_template_fqname,
                                      job_input,
                                      os_version,
                                      serial_num,
                                      onboarding_state)

    module.exit_json(**results)


if __name__ == '__main__':
    main()
