#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Dicover device utility.

This file contains untility functions for device discovery
"""

import ast
from builtins import next
from builtins import object
from builtins import str
import subprocess
import xml.etree.ElementTree as etree

from cfgm_common.exceptions import (
    RefsExistError,
    NoIdError
)
import paramiko
from vnc_api.gen.resource_client import PhysicalRouter
from vnc_api.vnc_api import VncApi

from job_manager.job_utils import JobFileWrite, JobVncApi
import json

REF_EXISTS_ERROR = 3
JOB_IN_PROGRESS = "JOB_IN_PROGRESS"


class DeviceInfo(object):
    output = {}

    def __init__(self, module):
        """Discover device utility initialization."""
        self.module = module
        self.logger = module.logger
        self.job_ctx = module.job_ctx
        self.fabric_uuid = module.params['fabric_uuid']
        self.total_retry_timeout = float(module.params['total_retry_timeout'])
        self._job_file_write = JobFileWrite(self.logger)
        self.credentials = []

    def initial_processing(self, concurrent):
        self.serial_num_flag = False
        self.all_serial_num = []
        serial_num = []
        self.per_greenlet_percentage = None

        self.job_ctx['current_task_index'] = 2

        try:
            total_percent = self.job_ctx.get('playbook_job_percentage')
            if total_percent:
                total_percent = float(total_percent)

            # Calculate the total percentage of this entire greenlet based task
            # This will be equal to the percentage alloted to this task in the
            # weightage array off the total job percentage. For example:
            # if the task weightage array is [10, 85, 5] and total job %
            # is 95. Then the 2nd task's effective total percentage is 85% of
            # 95%
            total_task_percentage = self.module.calculate_job_percentage(
                self.job_ctx.get('total_task_count'),
                task_seq_number=self.job_ctx.get('current_task_index'),
                total_percent=total_percent,
                task_weightage_array=self.job_ctx.get(
                    'task_weightage_array'))[0]

            # Based on the number of greenlets spawned (i.e num of sub tasks)
            # split the total_task_percentage equally amongst the greenlets.
            self.logger.info("Number of greenlets: {} and total_percent: "
                             "{}".format(concurrent, total_task_percentage))
            self.per_greenlet_percentage = \
                self.module.calculate_job_percentage(
                    concurrent, total_percent=total_task_percentage)[0]
            self.logger.info("Per greenlet percent: "
                             "{}".format(self.per_greenlet_percentage))

            self.vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                                 auth_token=self.job_ctx.get('auth_token'))
        except Exception as ex:
            self.logger.info("Percentage calculation failed with error "
                             "{}".format(str(ex)))

        try:
            self.vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                                 auth_token=self.job_ctx.get('auth_token'))
        except Exception as ex:
            self.module.results['failed'] = True
            self.module.results['msg'] = "Failed to connect to API server " \
                "due to error: %s"\
                % str(ex)
            self.module.exit_json(**self.module.results)

        # get credentials and serial number if greenfield
        if self.total_retry_timeout:
            # get serial numbers
            fabric_namespace_obj_list = self.vncapi.fabric_namespaces_list(
                parent_id=self.fabric_uuid, detail=True)
            fabric_namespace_list = self.vncapi.obj_to_dict(
                fabric_namespace_obj_list)

            for namespace in fabric_namespace_list:
                if namespace.get('fabric_namespace_type') == "SERIAL_NUM":
                    self.serial_num_flag = True
                    serial_num.append(namespace.get(
                        'fabric_namespace_value').get('serial_num'))

            if len(serial_num) > 1:
                for outer_list in serial_num:
                    for sn in outer_list:
                        self.all_serial_num.append(sn)

            device_auth = self.job_ctx['job_input'].get('device_auth', [])
            if device_auth:
                self.credentials = [{
                    'credential': {
                        'username': 'root',
                        'password': device_auth['root_password']}
                    }
                ]
        else:
            self.credentials = [
                {'credential': cred} for cred in
                self.job_ctx['job_input'].get('device_auth', [])]

    def ping_sweep(self, host):
        try:
            ping_output = subprocess.Popen(
                ['ping', '-W', '1', '-c', '1', host], stdout=subprocess.PIPE)
            ping_output.wait()
            return ping_output.returncode == 0
        except Exception as ex:
            self.logger.error("ERROR: SUBPROCESS.POPEN failed with error {}"
                              .format(str(ex)))
            return False
    # end _ping_sweep

    def _get_device_vendor(self, oid, vendor_mapping):
        for vendor in vendor_mapping:
            if vendor.get('oid') in oid:
                return vendor.get('vendor')
        return None
    # end _get_device_vendor

    def oid_mapping(self, host, pysnmp_output):
        matched_oid_mapping = {}
        matched_oid = None
        device_family_info = self.module.params['device_family_info']
        vendor_mapping = self.module.params['vendor_mapping']

        if pysnmp_output.get('ansible_sysobjectid'):
            vendor = self._get_device_vendor(
                pysnmp_output['ansible_sysobjectid'],
                vendor_mapping)
            if not vendor:
                self.logger.info("Vendor for host {} not supported".format(
                    host))
            else:
                device_family = next(
                    element for element in device_family_info
                    if element['vendor'] == vendor)
                if device_family:
                    try:
                        matched_oid = next(
                            item for item in device_family['snmp_probe']
                            if item['oid'] == pysnmp_output[
                                'ansible_sysobjectid'])
                    except StopIteration:
                        pass
                    if matched_oid:
                        matched_oid_mapping = matched_oid.copy()
                        matched_oid_mapping['hostname'] = \
                            pysnmp_output['ansible_sysname']
                        matched_oid_mapping['host'] = host
                        matched_oid_mapping['vendor'] = vendor
                    else:
                        self.logger.info(
                            "OID {} not present in the given list of device "
                            "info for the host {}".format(
                                pysnmp_output['ansible_sysobjectid'], host))
        return matched_oid_mapping
    # end _oid_mapping

    def _parse_xml_response(self, xml_response, oid_mapped):
        xml_response = xml_response.split('">')
        output = xml_response[1].split('<cli')
        final = etree.fromstring(output[0])
        if final.find('hardware-model') is not None:
            oid_mapped['product'] = final.find('hardware-model').text
        if final.find('os-name') is not None:
            oid_mapped['family'] = final.find('os-name').text
        if final.find('os-version') is not None:
            oid_mapped['os-version'] = final.find('os-version').text
        if final.find('serial-number') is not None:
            oid_mapped['serial-number'] = final.find('serial-number').text
        if final.find('host-name') is not None:
            oid_mapped['hostname'] = final.find('host-name').text
    # end _parse_xml_response

    def _ssh_connect(self, ssh_conn, username, password, hostname,
                     commands, oid_mapped):
        try:
            ssh_conn.connect(
                username=username,
                password=password,
                hostname=hostname)
            oid_mapped['username'] = username
            oid_mapped['password'] = password
            oid_mapped['host'] = hostname
        except Exception as ex:
            self.logger.info(
                "Could not connect to host {}: {}".format(
                    hostname, str(ex)))
            return False

        try:
            if commands:
                num_commands = len(commands) - 1
                for index, command in enumerate(commands):
                    stdin, stdout, stderr = ssh_conn.exec_command(
                        command['command'])
                    response = stdout.read()
                    if (not stdout and stderr) or (
                            response is None) or ('error' in response):
                        self.logger.info(
                            "Command {} failed on host {}:{}"
                            .format(command['command'], hostname, stderr))
                        if index == num_commands:
                            raise RuntimeError("All commands failed on host {}"
                                               .format(hostname))
                    else:
                        break
                self._parse_xml_response(response, oid_mapped)
            return True
        except RuntimeError as rex:
            self.logger.info("RunTimeError: {}".format(str(rex)))
            return False
        except Exception as ex:
            self.logger.info("SSH failed for host {}: {}".format(hostname,
                                                                 str(ex)))
            return False
# end _ssh_connect

    def get_device_info_ssh(self, host, oid_mapped, credentials):
        # find a credential that matches this host
        status = False
        device_family_info = self.module.params['device_family_info']

        sshconn = paramiko.SSHClient()
        sshconn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        try:
            for info in device_family_info:
                for cred in credentials:
                    status = self._ssh_connect(
                        sshconn,
                        cred['credential']['username'],
                        cred['credential']['password'],
                        host,
                        info['ssh_probe'],
                        oid_mapped)
                    if status:
                        oid_mapped['vendor'] = info['vendor']
                        break
        finally:
            sshconn.close()
            return status
    # end _get_device_info_ssh

    def _detailed_cred_check(self, host, oid_mapped, credentials):
        remove_null = []
        ssh_conn = paramiko.SSHClient()
        ssh_conn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        index = 0

        # check if credentials dict has both username and password defined.
        # If neither avaiable, remove the entire entry from the list.
        # Cannot check ssh connectivity with just the username or password.
        for creds in credentials[index:]:
            for user_pwd in list(creds.values()):
                if isinstance(user_pwd, dict):
                    if user_pwd.get('username') and user_pwd.get('password'):
                        index += 1
                        break
                    else:
                        credentials.remove(creds)
                        break

        # In a list of dict for credentials, if a dict value is None
        # remove the key from the dict. Only keys with values are retained.
        for single_dict in credentials:
            remove_null.append(
                dict([(dkey, ddata)
                      for dkey, ddata in list(single_dict.items()) if ddata]))

        # Sorting based on number of keys in a dict.Max-min sorting done here
        # resulting list would have dict with max keys as first entry
        # and min as the last
        prioritized_creds = sorted(remove_null, key=len, reverse=True)
        try:
            for device_cred in prioritized_creds:
                oid_vendor = oid_mapped['vendor']
                oid_family = oid_mapped['family']
                device_family = device_cred.get('device_family', None)
                vendor = device_cred.get('vendor', None)
                cred = device_cred.get('credential', None)
                username = cred.get('username', None) if cred else None
                password = cred.get('password', None) if cred else None

                if device_family and not vendor:
                    continue
                if vendor and vendor.lower() != oid_vendor.lower():
                    continue
                if vendor and device_family and device_family not in \
                        oid_family:
                    continue
                if not username or not password:
                    continue

                response = self._ssh_connect(
                    ssh_conn,
                    username,
                    password,
                    host,
                    None,
                    oid_mapped)
                if response:
                    return True

                self.logger.info(
                    "Credential for '{}' didn't work on host '{}'".format(
                        cred['credential']['username'], host))
            return False
        finally:
            ssh_conn.close()
    # end _ssh_check

    def _pr_object_create_update(
            self,
            oid_mapped,
            fq_name,
            update):
        pr_uuid = None
        msg = None
        dhcp_fq_name = None

        try:
            os_version = oid_mapped.get('os-version', None)
            serial_num = oid_mapped.get('serial-number', None)
            physicalrouter = PhysicalRouter(
                parent_type='global-system-config',
                fq_name=fq_name,
                physical_router_management_ip=oid_mapped.get('host'),
                physical_router_vendor_name=oid_mapped.get('vendor'),
                physical_router_product_name=oid_mapped.get('product'),
                physical_router_device_family=oid_mapped.get('family'),
                physical_router_vnc_managed=True,
                physical_router_snmp_credentials={
                    'version': 2,
                    'v2_community': 'public',
                    'local_port': 161
                },
                physical_router_hostname=fq_name[-1],
                display_name=fq_name[-1],
                physical_router_serial_number=serial_num,
                physical_router_managed_state='active',
                physical_router_user_credentials={
                    'username': oid_mapped.get('username'),
                    'password': oid_mapped.get('password')
                },
                physical_router_encryption_type = 'none',
                physical_router_supplemental_config=
                oid_mapped.get('supplemental_config')
            )
            if update:
                pr_unicode_obj = self.vncapi.physical_router_update(
                    physicalrouter)
                if pr_unicode_obj:
                    pr_obj_dict = ast.literal_eval(pr_unicode_obj)
                    pr_uuid = pr_obj_dict['physical-router']['uuid']
                    msg = "Discovered %s:\n   Host name: %s\n   Vendor: %s\n" \
                          "   Model: %s" % (
                              oid_mapped.get('host'),
                              fq_name[1],
                              oid_mapped.get('vendor'),
                              oid_mapped.get('product')
                          )
                    self.logger.info("Discovered {} : {}".format(
                        oid_mapped.get('host'), pr_uuid
                    ))
            else:
                # underlay_managed flag should only be set at physical-router
                # creation time
                physicalrouter.set_physical_router_underlay_managed(
                    self.job_ctx.get('job_input').get('manage_underlay', True)
                )
                pr_uuid = self.vncapi.physical_router_create(physicalrouter)
                msg = "Discovered device details: {} : {} : {}".format(
                    oid_mapped.get('host'), fq_name[1], oid_mapped.get(
                        'product'))
                self.logger.info("Device created with uuid- {} : {}".format(
                    oid_mapped.get(
                        'host'), pr_uuid))
            self.module.send_prouter_object_log(fq_name, "DISCOVERED",
                                                os_version, serial_num)
        except(RefsExistError, Exception) as ex:
            if isinstance(ex, RefsExistError):
                return REF_EXISTS_ERROR, None
            self.logger.error("VNC create failed with error: {}".format(str(
                ex)))
            return False, None

        try:
            # delete the corresponding dhcp state PR object if it exists
            dhcp_fq_name = ['default-global-system-config', oid_mapped.get(
                'host')]
            pr_obj = self.vncapi.physical_router_read(
                fq_name=dhcp_fq_name,fields=['physical_router_managed_state'])

            if pr_obj.get_physical_router_managed_state() == 'dhcp':
                self.vncapi.physical_router_delete(fq_name=dhcp_fq_name)
                self.logger.info(
                    "Router {} in dhcp state deleted".format(dhcp_fq_name))
        except(NoIdError, Exception) as ex:
            self.logger.info(
                "Router {} in dhcp state doesn't exist. Failed with "
                "error {}".format(dhcp_fq_name, str(ex)))
            pass

        self.module.send_job_object_log(
            msg,
            JOB_IN_PROGRESS,
            None,
            job_success_percent=self.per_greenlet_percentage)
        self.discovery_percentage_write()
        return True, pr_uuid

    def get_hostname_from_job_input(self, serial_num):
        hostname = None
        devices_to_ztp = self.job_ctx.get('job_input').get('device_to_ztp')
        for device_info in devices_to_ztp:
            if device_info.get('serial_number') == serial_num:
                hostname = device_info.get('hostname')
                break
        return hostname

    def get_supplemental_config(self, device_name):
        job_input = self.job_ctx.get('job_input')
        device_to_ztp = job_input.get('device_to_ztp')
        supplemental_configs = job_input.get('supplemental_day_0_cfg')
        supplemental_config = ""
        if device_name and device_to_ztp and supplemental_configs:
            device_map = dict((d.get('hostname', d.get('serial_number')), d)
                              for d in device_to_ztp)
            config_map = dict((c.get('name'), c) for c in supplemental_configs)
            if device_name in device_map:
                config_names = \
                    device_map[device_name].get('supplemental_day_0_cfg', [])
                if type(config_names) is not list:
                    config_names = [config_names]
                for config_name in config_names:
                    if config_name in config_map:
                        supplemental_config += \
                            config_map[config_name].get('cfg') + '\n'
        return supplemental_config

    def device_info_processing(self, host, oid_mapped):
        valid_creds = False
        return_code = True

        if not oid_mapped.get('family') or not oid_mapped.get('vendor'):
            self.logger.info("Could not retrieve family/vendor info for "
                             "the host: {}, not creating PR "
                             "object".format(host))
            self.logger.info("vendor: {}, family: {}".format(
                oid_mapped.get('vendor'), oid_mapped.get('family')))
            oid_mapped = {}

        if oid_mapped.get('host'):
            valid_creds = self._detailed_cred_check(host, oid_mapped,
                                                    self.credentials)

        if not valid_creds and oid_mapped:
            self.logger.info("No credentials matched for host: {}, nothing "
                             "to update in DB".format(host))
            oid_mapped = {}

        if oid_mapped:
            if self.serial_num_flag:
                if oid_mapped.get('serial-number') not in \
                        self.all_serial_num:
                    self.logger.info(
                        "Serial number {} for host {} not present "
                        "in fabric_namespace, nothing to update "
                        "in DB".format(
                            oid_mapped.get('serial-number'), host))
                    return

            # use the user input hostname is there. If its none check
            # for hostname derived from the device system info. If
            # that is also missing then set the hostname to the serial num
            user_input_hostname = None
            if self.job_ctx.get('job_input').get('device_to_ztp') is not None:
                user_input_hostname = \
                    self.get_hostname_from_job_input(oid_mapped.get(
                        'serial-number'))
            if user_input_hostname is not None:
                oid_mapped['hostname'] = user_input_hostname
            elif oid_mapped.get('hostname') is None:
                oid_mapped['hostname'] = oid_mapped.get('serial-number')

            # Get supplemental config for this device if it exists
            oid_mapped['supplemental_config'] = \
                self.get_supplemental_config(oid_mapped['hostname'])

            fq_name = [
                'default-global-system-config',
                oid_mapped.get('hostname')]
            return_code, pr_uuid = self._pr_object_create_update(
                oid_mapped, fq_name, False)
            if return_code == REF_EXISTS_ERROR:
                physicalrouter = self.vncapi.physical_router_read(
                    fq_name=fq_name)
                phy_router = self.vncapi.obj_to_dict(physicalrouter)
                if (phy_router.get('physical_router_management_ip') ==
                        oid_mapped.get('host')):
                    self.logger.info(
                        "Device with same mgmt ip already exists {}".format(
                            phy_router.get('physical_router_management_ip')))
                    return_code, pr_uuid = self._pr_object_create_update(
                        oid_mapped, fq_name, True)
                else:
                    fq_name = [
                        'default-global-system-config',
                        oid_mapped.get('hostname') +
                        '_' +
                        oid_mapped.get('host')]
                    return_code, pr_uuid = self._pr_object_create_update(
                        oid_mapped, fq_name, False)
                    if return_code == REF_EXISTS_ERROR:
                        self.logger.debug("Object already exists")
            if return_code is True:
                self.vncapi.ref_update(
                    "physical_router", pr_uuid, "fabric", self.fabric_uuid,
                    None, "ADD")
                self.logger.info(
                    "Fabric updated with physical router info for "
                    "host: {}".format(host))
                temp = {}
                temp['device_management_ip'] = oid_mapped.get('host')
                temp['device_fqname'] = fq_name
                temp['device_username'] = oid_mapped.get('username')
                temp['device_password'] = oid_mapped.get('password')
                temp['device_family'] = oid_mapped.get('family')
                temp['device_vendor'] = oid_mapped.get('vendor')
                temp['device_product'] = oid_mapped.get('product')
                temp['device_serial_number'] = oid_mapped.get('serial-number')
                DeviceInfo.output.update({pr_uuid: temp})

    def discovery_percentage_write(self):
        if self.module.results.get('percentage_completed'):
            exec_id = self.job_ctx.get('job_execution_id')
            pb_id = self.job_ctx.get('unique_pb_id')
            self._job_file_write.write_to_file(
                exec_id, pb_id, JobFileWrite.JOB_PROGRESS,
                str(self.module.results.get('percentage_completed'))
            )
