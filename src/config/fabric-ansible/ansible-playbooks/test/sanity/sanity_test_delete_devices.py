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
class SanityTestDeleteDevices(SanityBase):
    """
    Sanity test on deleting devices from fabric
    """

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_delete_devices')
    # end __init__

    def delete_fabric_devices(self, fabric_name, device_name):
        """delete devices in a fabric"""
        self._logger.info("Delete devices ...")
        job_execution_info = self._api.execute_job(
            job_template_fq_name=[
                'default-global-system-config', 'delete_device_template'],
            job_input={
                "fabric_fq_name": [
                    "default-global-system-config",
                    fabric_name
                ],
                "devices": [device_name]
            }
        )
        job_execution_id = job_execution_info.get('job_execution_id')
        self._logger.debug(
            "fabric deletion job started with execution id: %s", job_execution_id)
        self._wait_for_job_to_finish('fabric deletion', job_execution_id)
    # end delete_fabric 

    def test(self):
        try:
            self.delete_fabric_devices('fab01', 'DK588')
        except Exception as ex:
            self._exit_with_error(
                "Test failed due to unexpected error: %s" % str(ex))
    # end test


if __name__ == "__main__":
    SanityTestDeleteDevices(config.load('config/test_config.yml')).test()
# end __main__
