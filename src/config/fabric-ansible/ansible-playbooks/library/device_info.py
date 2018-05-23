#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""
import subprocess
import socket
import xml.etree.ElementTree as etree
import paramiko
from netaddr import IPNetwork
import ast
from cfgm_common.exceptions import (
    RefsExistError
)
from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import PhysicalRouter
from gevent import Greenlet, monkey, pool
monkey.patch_all()
from ansible.module_utils.fabric_pysnmp import snmp_walk
from ansible.module_utils.fabric_utils import FabricAnsibleModule

ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
For a given subnet or a list of IPs/hosts, check if they are reachable through
ICMP ping.
If reachable then using pysnmp get the device systemOID and map it to the
values in device_info , if not ssh into the device and get the system info
if juniper device.
Then check for ssh connectivity with a given list of credentials against all
reachable hosts.
If vendor and device family are specified, those credentials take precedence
for a given host matching the specifics.
'''

EXAMPLES = '''
device_info:
   credentials: [{
                "credential": {
                   "password": "*********",
                   "username": "root"
                },
                "device_family": "qfx",
                "vendor": "Juniper"
                }, {
                "credential": {
                   "password": "*********",
                   "username": "root"
                },
                "device_family": null,
                "vendor": "Juniper"
                }]
   subnets: [
             "10.155.67.0/29",
             "10.155.72.0/30"
            ]
   version: "v2c"
   community: "public"
   device_family_info: [
        {
            "snmp_probe": [
                {
                    "family": "junos-qfx",
                    "oid": "1.3.6.1.4.1.2636.1.1.1.4.82.8",
                    "product": "qfx5100"
                },
                {
                    "family": "junos-qfx",
                    "oid": "1.3.6.1.4.1.2636.1.1.1.4.82.5",
                    "product": "qfx5100"
                },
                {
                    "family": "junos",
                    "oid": "1.3.6.1.4.1.2636.1.1.1.2.29",
                    "product": "mx240"
                },
                {
                    "family": "junos",
                    "oid": "1.3.6.1.4.1.2636.1.1.1.2.11",
                    "product": "m10i"
                },
                {
                    "family": "junos",
                    "oid": "1.3.6.1.4.1.2636.1.1.1.2.57",
                    "product": "mx80"
                }
            ],
            "ssh_probe": {
                "command": "echo \"show system information | display xml\" |
                            cli",
                "data_format": "xml"
            },
            "vendor": "Juniper"
        }
    ]
'''

RETURN = '''

'''

REF_EXISTS_ERROR = 3
JOB_IN_PROGRESS = 1


def _parse_xml_response(xml_response, oid_mapped):
    xml_response = xml_response.split('">')
    output = xml_response[1].split('<cli')
    final = etree.fromstring(output[0])
    oid_mapped['product'] = final.find('hardware-model').text
    oid_mapped['family'] = final.find('os-name').text
    oid_mapped['os-version'] = final.find('os-version').text
    oid_mapped['serial-number'] = final.find('serial-number').text
    oid_mapped['hostname'] = final.find('host-name').text
# end _parse_xml_response

def _ssh_connect(ssh_conn, username, password, hostname,
                 commands, oid_mapped, logger):
    try:
        ssh_conn.connect(
            username=username,
            password=password,
            hostname=hostname)
        oid_mapped['username'] = username
        oid_mapped['password'] = password
        oid_mapped['host'] = hostname
        if commands:
            num_commands = len(commands) - 1
            for index, command in enumerate(commands):
                stdin, stdout, stderr = ssh_conn.exec_command(
                    command['command'])
                response = stdout.read()
                if (not stdout and stderr) or (response is None) or ('error'
                                                                     in response):
                    logger.info(
                        "Command {} failed on host {}:{}"
                        .format(command['command'], hostname, stderr))
                    if index == num_commands:
                        raise RuntimeError("All commands failed on host {}"
                                           .format(hostname))
                else:
                    break
            _parse_xml_response(response, oid_mapped)
        return True
    except RuntimeError as rex:
        logger.info("RunTimeError: {}".format(str(rex)))
        return False
    except Exception as ex:
        logger.info("SSH failed for host {}: {}".format(hostname, str(ex)))
        return False
# end _ssh_connect

def _ssh_check(host, credentials, oid_mapped, logger):
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

            response = _ssh_connect(
                ssh_conn,
                username,
                password,
                host,
                None,
                oid_mapped,
                logger)
            if response:
                return True

            logger.info(
                "Credential for '{}' didn't work on host '{}'".format(
                    cred['credential']['username'], host))
        return False
    finally:
        ssh_conn.close()
# end _ssh_check


def _ping_sweep(host, logger):
    try:
        ping_output = subprocess.Popen(['ping', '-W', '1', '-c', '1', host],
                                       stdout=subprocess.PIPE)
        ping_output.wait()
        return ping_output.returncode == 0
    except Exception as ex:
        logger.error("ERROR: SUBPROCESS.POPEN failed with error {}"
                     .format(str(ex)))
        return False
# end _ping_sweep


def _get_device_vendor(oid, vendor_mapping):
    for vendor in vendor_mapping:
        if vendor.get('oid') in oid:
            return vendor.get('vendor')
    return None
# end _get_device_vendor


def _oid_mapping(host, pysnmp_output, module):
    matched_oid_mapping = {}
    matched_oid = None
    device_family_info = module.params['device_family_info']
    vendor_mapping = module.params['vendor_mapping']
    logger = module.logger

    if pysnmp_output.get('ansible_sysobjectid'):
        vendor = _get_device_vendor(pysnmp_output['ansible_sysobjectid'],
                                    vendor_mapping)
        if not vendor:
            logger.info("Vendor for host {} not supported".format(host))
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
                    logger.info(
                        "OID {} not present in the given list of device "
                        "info for the host {}".format(
                            pysnmp_output['ansible_sysobjectid'], host))
    return matched_oid_mapping
# end _oid_mapping


def _get_device_info_ssh(host, oid_mapped, module):
    # find a credential that matches this host
    credentials = module.params['credentials']
    device_family_info = module.params['device_family_info']
    logger = module.logger

    sshconn = paramiko.SSHClient()
    sshconn.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        for info in device_family_info:
            for cred in credentials:
                success = _ssh_connect(
                    sshconn,
                    cred['credential']['username'],
                    cred['credential']['password'],
                    host,
                    info['ssh_probe'],
                    oid_mapped,
                    logger)
                if success:
                    oid_mapped['vendor'] = info['vendor']
                    break
    finally:
        sshconn.close()
# end _get_device_info_ssh


def _pr_object_create_update(
        vncapi,
        oid_mapped,
        fq_name,
        module,
        update):
    pr_uuid = None
    logger = module.logger
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
                logger.info("Updated device info for: {} : {}".format(oid_mapped.get(
                    'host'), pr_uuid))
        else:
            pr_uuid = vncapi.physical_router_create(physicalrouter)
            msg = "Discovered device details: {} : {} : {}".format(
                oid_mapped.get('host'), fq_name[1], oid_mapped.get('product'))
            logger.info("Device created with uuid- {} : {}".format(oid_mapped.get(
                'host'), pr_uuid))
            module.send_prouter_object_log(fq_name, "DISCOVERED",
                                               os_version, serial_num)
    except(RefsExistError, Exception) as ex:
        if isinstance(ex, RefsExistError):
            return REF_EXISTS_ERROR, None
        logger.error("VNC create failed with error: {}".format(str(ex)))
        return False, None

    module.send_job_object_log(msg, JOB_IN_PROGRESS, None)
    return True, pr_uuid

def _device_info_processing(host, vncapi, module, fabric_uuid):
    oid_mapped = {}
    valid_creds = False
    return_code = True
    credentials = module.params['credentials']
    logger = module.logger

    if _ping_sweep(host, logger):
        logger.info("HOST {}: REACHABLE".format(host))
        snmp_result = snmp_walk(host, '2vc', 'public')

        if snmp_result.get('error') is False:
            oid_mapped = _oid_mapping(host, snmp_result, module)
            if oid_mapped:
                logger.info("HOST {}: SNMP SUCCEEDED".format(host))
        else:
            logger.info(
                "SNMP failed for host {} with error {}".format(
                    host, snmp_result['error_msg']))

        if not oid_mapped or snmp_result.get('error') is True:
            _get_device_info_ssh(host, oid_mapped, module)
            if not oid_mapped.get('family') or not oid_mapped.get('vendor'):
                logger.info("Could not retrieve family/vendor info for the \
                    host: {}, not creating PR object".format(host))
                logger.info("vendor: {}, family: {}".format(
                    oid_mapped.get('vendor'), oid_mapped.get('family')))
                oid_mapped = {}

        if oid_mapped.get('host'):
            valid_creds = _ssh_check(host, credentials, oid_mapped, logger)

        if not valid_creds and oid_mapped:
            logger.info("No credentials matched for host: {}, nothing to update in DB".format(host))
            oid_mapped = {}

        if oid_mapped:
            fq_name = [
                'default-global-system-config',
                oid_mapped.get('hostname')]
            return_code, pr_uuid = _pr_object_create_update(
                vncapi, oid_mapped, fq_name, module, False)
            if return_code == REF_EXISTS_ERROR:
                physicalrouter = vncapi.physical_router_read(
                    fq_name=fq_name)
                phy_router = vncapi.obj_to_dict(physicalrouter)
                if (phy_router.get('physical_router_management_ip')
                        == oid_mapped.get('host')):
                    logger.info(
                        "Device with same mgmt ip already exists {}".format(
                            phy_router.get('physical_router_management_ip')))
                    return_code, pr_uuid = _pr_object_create_update(
                        vncapi, oid_mapped, fq_name, module, True)
                else:
                    fq_name = [
                        'default-global-system-config',
                        oid_mapped.get('hostname') +
                        '_' +
                        oid_mapped.get('host')]
                    return_code, pr_uuid = _pr_object_create_update(
                        vncapi, oid_mapped, fq_name, module,
                        False)
                    if return_code == REF_EXISTS_ERROR:
                        logger.debug("Object already exists")
            if return_code is True:
                vncapi.ref_update(
                    "fabric", fabric_uuid, "physical_router", pr_uuid, None, "ADD")
                logger.info("Fabric updated with physical router info for host: {}".format(host))
    else:
        logger.debug("HOST {}: NOT REACHABLE".format(host))
# end _device_info_processing


def _exit_with_error(module, msg):
    module.results['failed'] = True
    module.results['msg'] = msg
    module.send_job_object_log(module.results.get('msg'),
                               JOB_IN_PROGRESS, None)
    module.exit_json(**module.results)
# end _exit_with_error


def module_process(module):
    concurrent = module.params['pool_size']
    fabric_uuid = module.params['fabric_uuid']
    all_hosts = []

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net = IPNetwork(subnet)
                all_hosts.extend(list(ip_net))
            except Exception as ex:
                _exit_with_error(module, "ERROR: Invalid subnet \"%s\" (%s)" %
                                 (subnet, str(ex)))

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr = socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except Exception as ex:
                _exit_with_error(
                    module, "ERROR: Invalid ip address \"%s\" (%s)" %
                    (host, str(ex)))

    # Verify that we receive a community when using snmp v2
    if module.params['version'] == "v2" or module.params['version'] == "v2c":
        if module.params['community'] is None:
            _exit_with_error(module, "ERROR: Community not set when using \
                             snmp version 2")

    if module.params['version'] == "v3":
        _exit_with_error(module, "ERROR: Donot support snmp version 3")

    module.results['msg'] = "Prefix(es) to be discovered: " + \
        ','.join(module.params['subnets'])
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)

    if len(all_hosts) < concurrent:
        concurrent = len(all_hosts)

    threadpool = pool.Pool(concurrent)

    try:
        vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                        auth_token=module.job_ctx.get('auth_token'))
        for host in all_hosts:
            threadpool.start(
                Greenlet(
                    _device_info_processing,
                    str(host),
                    vncapi,
                    module,
                    fabric_uuid))
        threadpool.join()
    except Exception as ex:
        module.results['failed'] = True
        module.results['msg'] = "Failed to connect to API server due to error: %s"\
            % str(ex)
        module.exit_json(**module.results)

    module.results['msg'] = "Device discovery complete"
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)
    module.exit_json(**module.results)
# end module_process


def main():
    """module main"""
    module = FabricAnsibleModule(
        argument_spec=dict(
            fabric_uuid=dict(required=True),
            job_ctx=dict(type='dict', required=True),
            credentials=dict(required=True, type='list'),
            hosts=dict(type='list'),
            subnets=dict(type='list'),
            version=dict(required=True, choices=['v2', 'v2c', 'v3']),
            community=dict(required=True),
            device_family_info=dict(required=True, type='list'),
            vendor_mapping=dict(required=True, type='list'),
            pool_size=dict(default=500, type='int')
        ),
        supports_check_mode=True,
        required_one_of=[['hosts', 'subnets']]
    )

    module.execute(module_process)
# end main


if __name__ == '__main__':
    main()
