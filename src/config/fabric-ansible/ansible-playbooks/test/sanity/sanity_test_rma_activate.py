#!/usr/bin/python

#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#


from sanity_base import SanityBase
import config

test_underlay_config = "set cli screen-length 25"
test_serial_number = '123456789'

# pylint: disable=E1101
class SanityTestRmaActivate(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_rma_activate')
        rma = cfg['rma']
        self.fabric = rma['fabric']
        self.device_name = rma['device']
    # end __init__

    def rma_activate(self):
        self._logger.info("RMA activate ...")
        fabric_fq_name = ['default-global-system-config', self.fabric]
        device_fq_name = ['default-global-system-config', self.device_name]
        device_obj = self._api.physical_router_read(fq_name=device_fq_name)
        self.device_uuid = device_obj.uuid
        self.serial_number = device_obj.get_physical_router_serial_number() \
                             or test_serial_number
        self.managed_state = device_obj.get_physical_router_managed_state()
        self.underlay_config = device_obj.get_physical_router_underlay_config()
        self._logger.info("Device {}: serial_number={}, managed_state={}".\
                           format(self.device_name, self.serial_number,
                                  self.managed_state))

        # Use underlay config if already found, otherwise provide default
        if self.underlay_config:
            self._logger.info("Underlay config found: \'{}\'".\
                               format(self.underlay_config))
        else:
            self.underlay_config = test_underlay_config
            self._logger.info("Underlay config added: \'{}\'".\
                               format(self.underlay_config))
            device_obj.set_physical_router_underlay_config(self.underlay_config)

        # Change to rma state and verify
        device_obj.set_physical_router_managed_state('rma')
        self._api.physical_router_update(device_obj)
        device_obj = self._api.physical_router_read(fq_name=device_fq_name)
        self.managed_state = device_obj.get_physical_router_managed_state()
        if self.managed_state == 'rma':
            self._logger.info("State set to rma")
        else:
            self._logger.info("State is {} instead of rma. Exiting...".\
                               format(self.managed_state))
            return

        # Execute rma_activate job
        job_template_fq_name = [
            'default-global-system-config', 'rma_activate_template']
        job_input = {}
        if self.serial_number:
            job_input['serial_number'] = self.serial_number

        job_execution_info = self._api.execute_job(
            job_template_fq_name=job_template_fq_name,
            job_input=job_input,
            device_list = [self.device_uuid]
        )
        job_execution_id = job_execution_info.get('job_execution_id')
        self._logger.info(
            "RMA activate job started with execution id: %s", job_execution_id)
        self._wait_and_display_job_progress('RMA activate',
                                            job_execution_id, fabric_fq_name,
                                            job_template_fq_name)

        # Verify state change to active
        device_obj = self._api.physical_router_read(fq_name=device_fq_name)
        self.managed_state = device_obj.get_physical_router_managed_state()
        if self.managed_state == 'active':
            self._logger.info("State set to active")
        else:
            self._logger.info("State is {} instead of active. Exiting...".\
                               format(self.managed_state))
            return

        self._logger.info("... RMA activate complete")
    # end rma_activate


if __name__ == "__main__":
    SanityTestRmaActivate(config.load('config/test_config.yml')).rma_activate()
# end __main__
