#!/usr/bin/python

#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import time
from sanity_base import SanityBase
import config
from collections import namedtuple
from ansible.parsing.dataloader import DataLoader
from ansible.vars.manager import VariableManager
from ansible.inventory.manager import InventoryManager
from ansible.executor.playbook_executor import PlaybookExecutor

test_underlay_config = "set cli screen-length 25"

DEFAULT_ZEROIZE_TIMEOUT=10
DEFAULT_DEVICE_PASSWORD='Embe1mpls'
BOGUS_MGMT_MAC='00:11:22:33:44:55'
BOGUS_SERIAL_NUMBER='9999999999'

# pylint: disable=E1101
class SanityTestRmaActivate(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_rma_activate')
        self.api_server_host = cfg['api_server']['host']
        rma = cfg['rma']
        self.fabric = rma['fabric']
        self.device_name = rma['device']
        self.serial_number = rma['serial_number']
        self.zeroize_timeout = rma.get('timeout', DEFAULT_ZEROIZE_TIMEOUT)
        self.device_password = rma.get('password', DEFAULT_DEVICE_PASSWORD)
    # end __init__

    def rma_activate(self):
        self._logger.info("RMA activate ...")
        fabric_fq_name = ['default-global-system-config', self.fabric]
        fabric_obj = self._api.fabric_read(fq_name=fabric_fq_name)
        self.fabric_uuid = fabric_obj.uuid
        device_fq_name = ['default-global-system-config', self.device_name]
        device_obj = self._api.physical_router_read(fq_name=device_fq_name)
        self.device_uuid = device_obj.uuid
        self.managed_state = device_obj.get_physical_router_managed_state()
        self.underlay_config = device_obj.get_physical_router_underlay_config()
        self.mac = device_obj.get_physical_router_management_mac()
        self._logger.info("Device {}: serial_number={}, mac={}, managed_state={}".\
                           format(self.device_name, self.serial_number,
                                  self.mac, self.managed_state))

        # Use underlay config if already found, otherwise provide default
        if self.underlay_config:
            self._logger.info("Underlay config found: \'{}\'".\
                               format(self.underlay_config))
        else:
            self.underlay_config = test_underlay_config
            self._logger.info("Underlay config added: \'{}\'".\
                               format(self.underlay_config))
            device_obj.set_physical_router_underlay_config(self.underlay_config)

        # Change mac and serial number to bogus values to pretend like this
        # is another device. Then run update_dhcp_config playbook to install
        # mac into dnsmasq config file
        device_obj.set_physical_router_management_mac(BOGUS_MGMT_MAC)
        device_obj.set_physical_router_serial_number(BOGUS_SERIAL_NUMBER)
        self._api.physical_router_update(device_obj)

        playbook_input = {
            "device_management_ip": device_obj.physical_router_management_ip,
            "device_username": device_obj.physical_router_user_credentials.username,
            "device_password": self.device_password,
            "fabric_fq_name": fabric_fq_name,
            "fabric_uuid": self.fabric_uuid,
            "input": {"fabric_uuid": self.fabric_uuid},
            "api_server_host": [
                self.api_server_host
            ],
            "unique_pb_id": "e1d01b16-88ce-476a-9e83-066e4703274b",
            "auth_token": "",
            "job_execution_id": "1557873207326_641ccb71-be06-487a-972b-55ac90537fd2",
            "vnc_api_init_params": {
                "admin_password": "contrail123",
                "admin_tenant_name": "admin",
                "admin_user": "admin",
                "api_server_port": "8082",
                "api_server_use_ssl": "False"
            },
            "job_template_fqname": [
                "default-global-system-config",
                "rma_activate_template"
            ],
            "args": "{\"fabric_ansible_conf_file\": [\"/etc/contrail/contrail-keystone-auth.conf\", \"/etc/contrail/contrail-fabric-ansible.conf\"], \"zk_server_ip\": \"10.87.6.1:2181\", \"cluster_id\": \"\", \"host_ip\": \"10.87.6.1\", \"collectors\": [\"10.87.6.1:8086\"]}",
            "playbook_job_percentage": 28.5,
        }

        self.execute_playbook('../../update_dhcp_config.yml', playbook_input)

        # Now zeroize the device and wait for it to come back online
        # Since we don't know what IP address the device will be assigned,
        # we can't easily poll the device to see if it is back up
        # So the easiest thing to do is just delay
        self.execute_playbook('test_zeroize.yml', playbook_input)
        self._logger.info("Wait for {} minutes...".format(self.zeroize_timeout))
        time.sleep(self.zeroize_timeout*60)

        # Change to rma state and verify
        device_obj = self._api.physical_router_read(fq_name=device_fq_name)
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
        try:
            self._wait_and_display_job_progress('RMA activate',
                                            job_execution_id, fabric_fq_name,
                                            job_template_fq_name)
        except Exception as ex:
            self._exit_with_error(
                "Activate maintenance mode failed due to unexpected error: %s"
                % str(ex))
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

    def execute_playbook(self, playbook_name, playbook_input):
        try:
            loader = DataLoader()
            inventory = InventoryManager(loader=loader, sources=['localhost'])
            variable_manager = VariableManager(loader=loader,
                                               inventory=inventory)

            Options = namedtuple('Options',
                                 ['listtags', 'listtasks', 'listhosts',
                                  'syntax', 'connection', 'module_path',
                                  'forks', 'remote_user', 'private_key_file',
                                  'ssh_common_args', 'ssh_extra_args',
                                  'sftp_extra_args', 'scp_extra_args',
                                  'become', 'become_method', 'become_user',
                                  'verbosity', 'check', 'diff'])
            options = Options(listtags=False, listtasks=False, listhosts=False,
                              syntax=False, connection='ssh', module_path=None,
                              forks=100, remote_user=None,
                              private_key_file=None, ssh_common_args=None,
                              ssh_extra_args=None, sftp_extra_args=None,
                              scp_extra_args=None, become=None,
                              become_method=None, become_user=None,
                              verbosity=None, check=False, diff=False)

            variable_manager.extra_vars = {"playbook_input": playbook_input}

            pbex = PlaybookExecutor(playbooks=[playbook_name],
                                    inventory=inventory,
                                    variable_manager=variable_manager,
                                    loader=loader,
                                    options=options, passwords=None)
            ret_val = pbex.run()

            if ret_val != 0:
                msg = "playbook returned with error"
                self._logger.error(msg)
                raise Exception(msg)

        except Exception as exp:
            pass

if __name__ == "__main__":
    SanityTestRmaActivate(config.load('config/test_config.yml')).rma_activate()
# end __main__
