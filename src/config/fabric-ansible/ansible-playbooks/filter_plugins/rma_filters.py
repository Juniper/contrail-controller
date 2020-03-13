#!/usr/bin/python
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
# This file contains code to support the RMA devices feature
#

from builtins import object
from builtins import str
import sys
import traceback

from job_manager.job_utils import JobVncApi

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog, _task_error_log, get_job_transaction, \
    set_job_transaction  # noqa
from vnc_api.exceptions import NoIdError

class FilterModule(object):

    @staticmethod
    def _validate_job_ctx(job_ctx):
        if not job_ctx.get('fabric_fqname'):
            raise ValueError('Invalid job_ctx: missing fabric_fqname')
        job_input = job_ctx.get('job_input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')
        return job_input
    # end _validate_job_ctx

    def filters(self):
        return {
            'rma_activate_devices': self.rma_activate_devices,
            'rma_devices_to_ztp': self.rma_devices_to_ztp,
            'rma_get_device_password': self.rma_get_device_password
        }
    # end filters

    #retrieve first device's password
    def rma_get_device_password(self, job_ctx, rma_devices_list):
        try:
            self.vncapi = JobVncApi.vnc_init(job_ctx)
            device_uuid = rma_devices_list[0]['device_uuid']
            device_obj = self.vncapi.physical_router_read(id=device_uuid)
            return self._get_password(device_obj)
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }

    # Wrapper to call main routine
    def rma_devices_to_ztp(self, job_ctx, rma_devices_list):
        try:
            FilterLog.instance("RmaDevicesFilter")
            self.job_input = FilterModule._validate_job_ctx(job_ctx)
            self.vncapi = JobVncApi.vnc_init(job_ctx)
            self.job_ctx = job_ctx
            return self._rma_devices_to_ztp(rma_devices_list)
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end rma_devices_to_ztp

    def _rma_devices_to_ztp(self, rma_devices_list):
        devices_to_ztp = []
        for dev_info in rma_devices_list:
            devices_to_ztp.append(
                {'serial_number': dev_info.get('serial_number')})
        return devices_to_ztp

    # Wrapper to call main routine
    def rma_activate_devices(self, job_ctx, rma_devices_list, lease_tbl):
        try:
            FilterLog.instance("RmaDevicesFilter")
            self.job_input = FilterModule._validate_job_ctx(job_ctx)
            self.vncapi = JobVncApi.vnc_init(job_ctx)
            self.job_ctx = job_ctx
            self.device_to_ztp = []
            self.supplemental_config = []
            return self._rma_activate_devices(rma_devices_list, lease_tbl)
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end rma_activate_devices

    def _rma_activate_devices(self, rma_devices_list, lease_tbl):
        device_info = {}
        ip_tbl = {}
        for dev_info in rma_devices_list:
            device_uuid = dev_info['device_uuid']
            new_serial_number = dev_info.get('serial_number')
            mgmt_ip = self._get_mgmt_ip(lease_tbl['device_list'],
                                        new_serial_number)
            device_obj = self.vncapi.physical_router_read(id=device_uuid)
            device_name = device_obj.name
            device_cli_obj_name = device_name + "_" + "cli_config"
            cli_obj_fq_name = ['default-global-system-config', device_name, device_cli_obj_name]
            underlay_managed = \
                device_obj.get_physical_router_underlay_managed()

            # Only handle greenfield devices
            if not underlay_managed:
                raise Exception("Device {} not underlay managed".format(
                    device_name))

            # if serial number not found, go into error state on this device
            if not mgmt_ip:
                device_obj.set_physical_router_managed_state('error')
                self.vncapi.physical_router_update(device_obj)
                continue

            # Try to read cli config object if present, else return empty string for config
            try:
                cli_obj = self.vncapi.cli_config_read(fq_name=cli_obj_fq_name)
                accepted_cli_config = cli_obj.get_accepted_cli_config()
            except NoIdError:
                accepted_cli_config = ""

            # Get device supplemental config, if any, and append to overall
            # supplemental config
            device_supp_cfg = \
                device_obj.get_physical_router_supplemental_config()
            if not device_supp_cfg:
                device_supp_cfg = ""
            self._supplemental_config_append(device_name,
                                             new_serial_number,
                                             device_supp_cfg, accepted_cli_config)
            temp = {}
            temp['device_management_ip'] = \
                device_obj.get_physical_router_management_ip()
            temp['device_fqname'] = device_obj.fq_name
            temp['device_username'] = \
                device_obj.physical_router_user_credentials.username
            temp['device_password'] = self._get_password(device_obj)
            temp['device_family'] = \
                device_obj.get_physical_router_device_family()
            temp['device_vendor'] = \
                device_obj.get_physical_router_vendor_name()
            temp['device_product'] = \
                device_obj.get_physical_router_product_name()
            temp['device_os_version'] = \
                device_obj.get_physical_router_os_version()
            temp['device_serial_number'] = new_serial_number
            temp['device_dynamic_mgmt_ip'] = mgmt_ip
            device_info.update({device_uuid: temp})

            # Update serial number on object and save
            device_obj.set_physical_router_serial_number(new_serial_number)

            # Update job transaction ID
            trans_info = get_job_transaction(self.job_ctx)
            set_job_transaction(device_obj, None, trans_info)

            self.vncapi.physical_router_update(device_obj)

            ip_tbl.update({device_uuid: {'dynamic_mgmt_ip': mgmt_ip}})

        rma_device_info = {
            'device_info': device_info,
            'dynamic_mgmt_ip_tbl': ip_tbl,
            'device_to_ztp': self.device_to_ztp,
            'supplemental_config': self.supplemental_config
        }
        return rma_device_info

    def _get_mgmt_ip(self, lease_tbl, serial_number):
        for lease in lease_tbl:
            if lease['host_name'] == serial_number:
                return lease['ip_addr']
        return None

    # Get device password
    def _get_password(self, device_obj):
        return JobVncApi.decrypt_password(
            encrypted_password=device_obj.physical_router_user_credentials.
            get_password(),
            pwd_key=device_obj.uuid)

    # Recreate supplemental config using only the RMA devices
    def _supplemental_config_append(self, device_name, serial_number,
                                    device_supp_cfg, acc):

        if not acc:
            acc = ""
        dev2ztp_entry = {
            'hostname': device_name,
            'serial_number': serial_number
        }

        if device_supp_cfg:
            cfg_name = device_name + '_cfg'
            supp_cfg_entry = {
                'name': cfg_name,
                'cfg': device_supp_cfg + acc
            }
            self.supplemental_config.append(supp_cfg_entry)

            dev2ztp_entry['supplemental_day_0_cfg'] = cfg_name

        self.device_to_ztp.append(dev2ztp_entry)

