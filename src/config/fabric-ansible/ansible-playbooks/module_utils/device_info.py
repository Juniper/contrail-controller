#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains untility functions for device discovery
"""
import subprocess
import paramiko
import xml.etree.ElementTree as etree
import ast
import time
from datetime import datetime
from cfgm_common.exceptions import (
    RefsExistError
)
from vnc_api.gen.resource_client import PhysicalRouter

REF_EXISTS_ERROR = 3
JOB_IN_PROGRESS = "JOB_IN_PROGRESS"

class DeviceInfo(object):
    output = {}

    def __init__(self, module):
        self.module = module
        self.logger = module.logger


    def ping_sweep(self, host):
        try:
            ping_output = subprocess.Popen(['ping', '-W', '1', '-c', '1', host],
                                           stdout=subprocess.PIPE)
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
                            if item['oid'] == pysnmp_output['ansible_sysobjectid'])
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
        oid_mapped['product'] = final.find('hardware-model').text
        oid_mapped['family'] = final.find('os-name').text
        oid_mapped['os-version'] = final.find('os-version').text
        oid_mapped['serial-number'] = final.find('serial-number').text
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
            self.logger.info("Could not connect to host {}: {}".format(hostname,
                                                          str(ex)))
            return False

        try:
            if commands:
                num_commands = len(commands) - 1
                for index, command in enumerate(commands):
                    stdin, stdout, stderr = ssh_conn.exec_command(
                        command['command'])
                    response = stdout.read()
                    if (not stdout and stderr) or (response is None) or ('error'
                                                                         in response):
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


    def detailed_cred_check(self, host, oid_mapped, credentials):
        remove_null = []
        ssh_conn = paramiko.SSHClient()
        ssh_conn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        index = 0

        # check if credentials dict has both username and password defined.
        # If neither avaiable, remove the entire entry from the list.
        # Cannot check ssh connectivity with just the username or password.
        for creds in credentials[index:]:
            for user_pwd in creds.values():
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
                      for dkey, ddata in single_dict.iteritems() if ddata]))

        # Sorting based on number of keys in a dict. Max to min sorting done here
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
                if vendor and device_family and device_family not in oid_family:
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

    def pr_object_create_update(
        self,
        vncapi,
        oid_mapped,
        fq_name,
        update,
        per_greenlet_percentage):
        pr_uuid = None
        msg = None
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
                physical_router_user_credentials={
                    'username': oid_mapped.get('username'),
                    'password': oid_mapped.get('password')
                }
            )
            if update:
                pr_unicode_obj = vncapi.physical_router_update(physicalrouter)
                if pr_unicode_obj:
                    pr_obj_dict = ast.literal_eval(pr_unicode_obj)
                    pr_uuid = pr_obj_dict['physical-router']['uuid']
                    msg = "Updated device info for: {} : {} : {}".format(
                        oid_mapped.get('host'), fq_name[1], oid_mapped.get('product'))
                    self.logger.info("Updated device info for: {} : {}".format(
                        oid_mapped.get(
                        'host'), pr_uuid))
            else:
                pr_uuid = vncapi.physical_router_create(physicalrouter)
                msg = "Discovered device details: {} : {} : {}".format(
                    oid_mapped.get('host'), fq_name[1], oid_mapped.get('product'))
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

        self.module.send_job_object_log(msg, JOB_IN_PROGRESS, None,
                                        job_success_percent=per_greenlet_percentage)
        return True, pr_uuid
