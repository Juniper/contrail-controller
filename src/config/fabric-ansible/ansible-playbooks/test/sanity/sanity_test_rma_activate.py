#!/usr/bin/python

#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#


from sanity_base import SanityBase
import config


# pylint: disable=E1101
class SanityTestRmaActivate(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_rma_activate')
        rma = cfg['rma']
        self.fabric = rma['fabric']
        self.device_name = rma['device']
        self.serial_number = rma.get('serial_number')
        self.hostname = rma.get('hostname')
    # end __init__

    def rma_activate(self):
        self._logger.info("RMA activate ...")
        fabric_fq_name = ['default-global-system-config', self.fabric]
        device_fq_name = ['default-global-system-config', self.device_name]
        device_uuid = self._api.physical_router_read(
            fq_name=device_fq_name).uuid
        job_template_fq_name = [
            'default-global-system-config', 'rma_activate_template']
        job_input = {}
        if self.serial_number:
            job_input['serial_number'] = self.serial_number
        if self.hostname:
            job_input['hostname'] = self.hostname

        job_execution_info = self._api.execute_job(
            job_template_fq_name=job_template_fq_name,
            job_input=job_input,
            device_list = [device_uuid]
        )
        job_execution_id = job_execution_info.get('job_execution_id')
        self._logger.debug(
            "RMA activate job started with execution id: %s", job_execution_id)
        self._wait_and_display_job_progress('RMA activate',
                                            job_execution_id, fabric_fq_name,
                                            job_template_fq_name)
        self._logger.info("... RMA activate complete")
    # end rma_activate


if __name__ == "__main__":
    SanityTestRmaActivate(config.load('config/test_config.yml')).rma_activate()
# end __main__
