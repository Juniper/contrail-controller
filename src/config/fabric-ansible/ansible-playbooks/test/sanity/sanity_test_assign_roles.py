#!/usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains sanity test for all major workflows supported by
fabric ansible
"""
from __future__ import absolute_import

from builtins import str
from .sanity_base import SanityBase
from . import config


# pylint: disable=E1101
class SanityTestAssignRoles(SanityBase):
    """
    Sanity test on role assignment
    """

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_assign_roles')
    # end __init__

    def assign_roles(self, role_assignment):
        """Assign roles to devices"""
        self._logger.info("Role assignment ...")
        job_template_fq_name = [
            'default-global-system-config', 'role_assignment_template']
        fabric_fq_name = role_assignment['fabric_fq_name']

        job_execution_info = self._api.execute_job(
            job_template_fq_name=job_template_fq_name,
            job_input=role_assignment
        )
        job_execution_id = job_execution_info.get('job_execution_id')
        self._logger.debug(
            "Role assignment job started with execution id: %s", job_execution_id)
        self._wait_and_display_job_progress('Role assignment', job_execution_id,
                                            fabric_fq_name, job_template_fq_name)
        self._logger.info("... Role assignment complete")
    # end assign_roles

    def test(self):
        try:
            self.assign_roles(
                role_assignment={
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "role_assignments": [
                        {
                            "device_fq_name": [
                                "default-global-system-config",
                                "DK588"
                            ],
                            "physical_role": "spine",
                            "routing_bridging_roles": ["centrally-routed-bridging"]
                        },
                        {
                            "device_fq_name": [
                                "default-global-system-config",
                                "VF3717350117"
                            ],
                            "physical_role": "leaf"
                        }
                    ]
                }
            )
        except Exception as ex:
            self._exit_with_error(
                "Test failed due to unexpected error: %s" % str(ex))
    # end test


if __name__ == "__main__":
    SanityTestAssignRoles(config.load('sanity/config/test_config.yml')).test()
# end __main__
