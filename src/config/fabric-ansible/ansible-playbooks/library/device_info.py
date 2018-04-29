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
from cfgm_common.exceptions import (
    RefsExistError
)
from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import PhysicalRouter
from ansible.module_utils.fabric_pysnmp import snmp_walk
from ansible.module_utils.fabric_utils import FabricAnsibleModule
from gevent import Greenlet, monkey, pool
monkey.patch_all()
from ansible.module_utils.sandesh_log_utils import ObjectLogUtil

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
                "data-format": "xml"
            },
            "vendor": "Juniper"
        }
    ]
'''

RETURN = '''

'''

_result = {}
REF_EXISTS_ERROR = 3
POOL_SIZE = 500
JOB_IN_PROGRESS = 1

def parse_xml_response(xml_response, oid_mapped):
    xml_response = xml_response.split('">')
    output = xml_response[1].split('<cli')
    final = etree.fromstring(output[0])
    oid_mapped['product'] = final.find('hardware-model').text
    oid_mapped['family'] = final.find('os-name').text
    oid_mapped['hostname'] = final.find('host-name').text

def ssh_connect(ssh_conn, username, password, hostname, command, oid_mapped,
                logger):
    try:
        ssh_conn.connect(
            username=username,
            password=password,
            hostname=hostname)
        oid_mapped['username'] = username
        oid_mapped['password'] = password
        oid_mapped['host'] = hostname
        if command:
            stdin, stdout, stderr = ssh_conn.exec_command(command)
            if stderr:
                raise RunTimeError("Command {} failed on host {}:{}".format(command, hostname, stderr))
            response = stdout.read()
            if response is None:
                raise RunTimeError("Command {} succeeded on host with no response {}".format(command, hostname))
            parse_xml_response(response, oid_mapped)
        return True
    except RunTimeError as rex:
        logger.info("RunTimeError: {}".format(str(rex)))
        return False
    except Exception as ex:
        logger.info("SSH failed for host {}".format(hostname))
        return False

def ssh_check(host, credentials, oid_mapped, logger):
    remove_null = []
    ssh_conn = paramiko.SSHClient()
    ssh_conn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    index = 0

    """
    check if credentials dict has both username and password defined.
    If neither avaiable, remove the entire entry from the list.
    Cannot check ssh connectivity with just the username or password.
    """
    for creds in credentials[index:]:
        for user_pwd in creds.values():
            if isinstance(user_pwd, dict):
                if user_pwd.get('username') and user_pwd.get('password'):
                    index += 1
                    break
                else:
                    credentials.remove(creds)
                    break

    """
    In a list of dict for credentials, if a dict value is None
    remove the key from the dict. Only keys with values are retained.
    """
    for single_dict in credentials:
        remove_null.append(
            dict([(dkey, ddata)
                  for dkey, ddata in single_dict.iteritems() if ddata]))
    """
    Sorting based on number of keys in a dict. Max to min sorting done here
    resulting list would have dict with max keys as first entry
    and min as the last
    """
    sorted_len = sorted(remove_null, key=len, reverse=True)

    for cred in sorted_len:
        if (('device_family' in cred and 'vendor' not in cred) or \
            (('vendor' in cred) and \
             ((cred['vendor'].lower() != oid_mapped['vendor'].lower()) or \
              ('device_family' in cred and cred['device_family'] not in \
               oid_mapped['family'])))):
            continue
        else:
            response = ssh_connect(
                ssh_conn,
                cred['credential']['username'],
                cred['credential']['password'],
                host,
                None,
                oid_mapped,
                logger)
            if response:
                return True
            else:
                logger.info(
                    "Credentials didn't work for \
                        host {} with credentials {}:{}".format(host, \
                        cred['credential']['username'], \
                        cred['credential']['password']))
                return False


def ping_sweep(host, logger):
    try:
        ping_output = subprocess.Popen(['ping', '-W', '1', '-c', '1', host],
                                       stdout=subprocess.PIPE)
        ping_output.wait()
        if ping_output.returncode == 0:
            return True
        else:
            return False
    except Exception as ex:
        logger.error("ERROR: SUBPROCESS.POPEN failed with error {}"
                     .format(str(ex)))
        return False


def get_device_vendor(oid, vendor_mapping):
    for vendor in vendor_mapping:
        if vendor.get('oid') in oid:
            return vendor.get('vendor')
        else:
            ""


def oid_mapping(host, pysnmp_output, module):
    matched_oid_mapping = {}
    device_family_info = module.params['device_family_info']
    vendor_mapping = module.params['vendor_mapping']
    logger = module.logger

    if pysnmp_output.get('ansible_sysobjectid'):
        vendor = get_device_vendor(pysnmp_output['ansible_sysobjectid'],
                                   vendor_mapping)
        if not vendor:
            logger.debug("Vendor for host {} not supported".format(host))
        else:
            device_family = next(element for element in device_family_info
                                 if element['vendor'] == vendor)
            if device_family:
                try:
                    matched_oid_mapping = next(item for item in device_family[
                        'snmp_probe'] if item['oid'] == \
                            pysnmp_output['ansible_sysobjectid'])
                except StopIteration:
                    pass
                if matched_oid_mapping:
                    matched_oid_mapping['hostname'] = \
                        pysnmp_output['ansible_sysname']
                    matched_oid_mapping['host'] = host
                    matched_oid_mapping['vendor'] = vendor
                else:
                    logger.debug(
                        "OID {} not present in the given list of device "
                        "info for the host {}".format(
                            pysnmp_output['ansible_sysobjectid'], host))
    return matched_oid_mapping


def get_device_info_ssh(host, oid_mapped, module):
    # find a credential that matches this host
    credentials = module.params['credentials']
    device_family_info = module.params['device_family_info']
    logger = module.logger

    sshconn = paramiko.SSHClient()
    sshconn.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    for info in device_family_info:
        for cred in credentials:
            success = ssh_connect(
                sshconn,
                cred['credential']['username'],
                cred['credential']['password'],
                host,
                info['ssh_probe']['command'],
                oid_mapped,
                logger)
            if success:
                oid_mapped['vendor'] = info['vendor']
                break

def pr_object_create(vncapi, oid_mapped, fq_name, logger, object_log, fabric_uuid):
    try:
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
        pr_uuid = vncapi.physical_router_create(physicalrouter)
    except(RefsExistError, Exception) as ex:
        if RefsExistError:
            return REF_EXISTS_ERROR
        else:
            logger.error("VNC create failed with error: {}".format(str(
                ex)))
            return False
    msg = "Discovered device details: {} : {} : {}".format(oid_mapped.get('host'),
                                                       fq_name[1],
                                                       oid_mapped.get('product'))
    logger.info("Device created with uuid- {} : {}".format(oid_mapped.get(
        'host'), pr_uuid))
    vncapi.ref_update("fabric", fabric_uuid, "physical_router", pr_uuid, None, "ADD")
    send_sandesh_log(object_log, msg, logger, False)
    return True


def device_info_processing(host, vncapi, module, object_log, fabric_uuid):
    oid_mapped = {}
    valid_creds = False
    credentials = module.params['credentials']
    logger = module.logger

    if ping_sweep(host, logger):
        snmp_result = snmp_walk(host, '2vc', 'public')

        if snmp_result.get('error') is False:
            oid_mapped = oid_mapping(host, snmp_result, module)
        else:
            logger.debug(
                "SNMP failed for host {} with error {}".format(
                    host, snmp_result['error_msg']))

        if not oid_mapped or snmp_result.get('error') is True:
            get_device_info_ssh(host, oid_mapped, module)

        if oid_mapped.get('host'):
            valid_creds = ssh_check(host, credentials, oid_mapped, logger)

        if not valid_creds:
            logger.debug("No credentials matched, nothing to update in DB")
            oid_mapped = {}

        if oid_mapped:
            fq_name = [
                'default-global-system-config',
                oid_mapped.get('hostname')]
            return_code = pr_object_create(vncapi, oid_mapped, fq_name, logger, object_log, fabric_uuid)
            if return_code == REF_EXISTS_ERROR:
                physicalrouter = vncapi.physical_router_read(
                    fq_name=fq_name)
                phy_router = vncapi.obj_to_dict(physicalrouter)
                if phy_router.get('physical_router_management_ip') == \
                   oid_mapped.get('host'):
                    logger.info(
                        "Device with same mgmt ip already exists {}".format(
                            phy_router.get('physical_router_management_ip')))
                else:
                    fq_name = [
                        'default-global-system-config',
                        oid_mapped.get('hostname') +
                        '_' +
                        oid_mapped.get('host')]
                    return_code = pr_object_create(vncapi, oid_mapped, \
                                                   fq_name, logger, object_log, fabric_uuid)
                    if return_code == REF_EXISTS_ERROR:
                        logger.debug("Object already exists")
    else:
        logger.debug("HOST {}: NOT REACHABLE".format(host))

def send_sandesh_log(object_log, msg, logger, close):
    try:
        object_log.send_job_object_log(
            msg, JOB_IN_PROGRESS, None)
    except ValueError as ve:
        logger.error(str(ve))
    except Exception as ex:
        logger.error("Unable to log sandesh job logs: %s", str(ex))

    if close:
        object_log.close_sandesh_conn()

def main():
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
    concurrent = module.params['pool_size']
    fabric_uuid = module.params['fabric_uuid']
    all_hosts = []
    job_ctx = module.params['job_ctx']
    object_log = ObjectLogUtil(job_ctx)

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net = IPNetwork(subnet)
                all_hosts.extend(list(ip_net))
            except Exception as ex:
                _result['failed'] = True
                _result['msg'] = "ERROR: Invalid subnet \"%s\" (%s)" % \
                                 (subnet, str(ex))
                send_sandesh_log(object_log, _result.get('msg'),
                                 module.logger, True)
                module.exit_json(**_result)

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr = socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except Exception as ex:
                _result['failed'] = True
                _result['msg'] = "ERROR: Invalid ip address \"%s\" (%s)" % \
                                 (host, str(ex))
                send_sandesh_log(object_log, _result.get('msg'),
                                 module.logger, True)
                module.exit_json(**_result)

    # Verify that we receive a community when using snmp v2
    if module.params['version'] == "v2" or module.params['version'] == "v2c":
        if module.params['community'] is None:
            _result['failed'] = True
            _result['msg'] = "ERROR: Community not set when using \
                             snmp version 2"
            send_sandesh_log(object_log, _result.get('msg'), module.logger,
                             True)
            module.exit_json(**_result)

    if module.params['version'] == "v3":
        _result['failed'] = True
        _result['msg'] = "ERROR: Donot support snmp version 3"
        send_sandesh_log(object_log, _result.get('msg'), module.logger,
                         True)
        module.exit_json(**_result)

    _result['msg'] = "Prefix(es) to be discovered: " + \
                     ','.join(module.params['subnets'])
    send_sandesh_log(object_log, _result.get('msg'), module.logger, False)

    if len(all_hosts) < concurrent:
        concurrent = len(all_hosts)

    threadpool = pool.Pool(concurrent)

    try:
        vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                        auth_token=job_ctx.get('auth_token'))
        for host in all_hosts:
            threadpool.start(Greenlet(device_info_processing,
                                      str(host), vncapi, module, object_log, fabric_uuid))
        threadpool.join()
    except Exception as ex:
        _result['failed'] = True
        _result['msg'] = "Failed to connect to API server due to error: %s"\
            % str(ex)
        module.exit_json(**_result)

    _result['msg'] = "Device discovery complete"
    send_sandesh_log(object_log, _result.get('msg'), module.logger, True)
    module.exit_json(**_result)


if __name__ == '__main__':
    main()
