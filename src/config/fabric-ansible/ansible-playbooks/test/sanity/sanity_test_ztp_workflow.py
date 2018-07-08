#!/usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains sanity test for all major workflows supported by
fabric ansible
"""

from sanity_base import SanityBase
import config


# pylint: disable=E1101
class SanityTestZtpWorkflow(SanityBase):
    """
    Sanity test on full ztp workflow:
     - fabric onboard
     - ztp
     - device discovery
     - role discovery
     - device import
     - topology discovery
    """

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_ztp_workflow')
        self._namespaces = cfg['namespaces']
        self._prouter = cfg['prouter']
    # end __init__

    def onboard_fabric(self, fabric_info):
        """Onboard fabric"""
        self._logger.info("Onboard fabric ...")
        job_execution_info = self._api.execute_job(
            job_template_fq_name=[
                'default-global-system-config', 'fabric_onboard_template'],
            job_input=fabric_info
        )

        job_execution_id = job_execution_info.get('job_execution_id')
        self._logger.debug(
            "Onboard fabric job started with execution id: %s", job_execution_id)
        self._wait_for_job_to_finish('Onboard fabric', job_execution_id)
    # end onboard_fabric

    def test(self):
        try:
            self.cleanup_fabric('fab01')
            self.onboard_fabric(
                fabric_info={
                    "fabric_fq_name": ["default-global-system-config", "fab01"],
                    "device_auth": {
                        "root_password": "Embe1mpls"
                    },
                    "fabric_asn_pool": [
                        {
                            "asn_max": 65000,
                            "asn_min": 64000
                        },
                        {
                            "asn_max": 65100,
                            "asn_min": 65000
                        }
                    ],
                    "fabric_subnets": [
                        "30.1.1.1/24"
                    ],
                    "loopback_subnets": [
                        "20.1.1.1/24"
                    ],
                    "management_subnets": [
                        { "cidr": "192.168.10.1/24", "gateway": "192.168.10.1" }
                    ],
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5100"
                        },
                        {
                            "node_profile_name": "juniper-qfx10002"
                        }
                    ],
                    "device_count": 1
                }
            )
        except Exception as ex:
            self._exit_with_error(
                "Test failed due to unexpected error: %s" % str(ex))
    # end test


if __name__ == "__main__":
    SanityTestZtpWorkflow(config.load('config/test_config.yml')).test()
# end __main__
