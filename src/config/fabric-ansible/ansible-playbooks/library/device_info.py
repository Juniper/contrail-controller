#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""

__metaclass__ = type

from ansible.module_utils.basic import AnsibleModule
import paramiko
import subprocess
import socket
from netaddr import IPNetwork
from ansible.module_utils.fabric_pysnmp import snmp_walk
from ansible.module_utils.sandesh_log_utils import ObjectLogUtil
import xml.etree.ElementTree as etree
import logging
import gevent
from gevent import Greenlet, monkey, getcurrent
monkey.patch_all()

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
rechables hosts.
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
List of hostnames mapped to their family vemdor and valid credentials
'''

_result = {}


def ssh_connect(ssh_conn, username, password, hostname, command, mapped_value):
    try:
        ssh_conn.connect(
            username=username,
            password=password,
            hostname=hostname)
        mapped_value['username'] = username
        mapped_value['password'] = password
        mapped_value['host'] = hostname
        if command:
            stdin, stdout, stderr = ssh_conn.exec_command(command)
            output = stdout.read()
            output = output.split('">')
            output1 = output[1].split('<cli')
            final = etree.fromstring(output1[0])
            mapped_value['product'] = final.find('hardware-model').text
            mapped_value['family'] = final.find('os-name').text
            mapped_value['hostname'] = final.find('host-name').text
        return True
    except(paramiko.BadHostKeyException,
           paramiko.AuthenticationException,
           paramiko.SSHException,
           Exception) as e:
        return False


def ssh_check(host, credentials, mapped_value):
    remove_null = []
    ssh_conn = paramiko.SSHClient()
    ssh_conn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    index = 0

    """
    check if credentials dict has both username and password defined.
    If neither avaiable, remove the entire entry from the list.
    Cannot check ssh connectivity with just the username/password.
    """
    for outer_dict in credentials[index:]:
        for inner_dict in outer_dict.values():
            if isinstance(inner_dict, dict):
                if inner_dict['username'] and inner_dict['password']:
                    index += 1
                    break
                else:
                    credentials.remove(outer_dict)
                    index = index
                    break

    """
    In a list of dict for credentials, if a dict value is None
    remove the key from the dict. Only keys with values are retained.
    """
    for single_dict in credentials:
        remove_null.append(
            dict([(dkey, ddata)
                  for dkey, ddata in single_dict.iteritems() if(ddata)]))
    """
    Sorting based on number of keys in a dict. Max to min sorting done here
    resulting list would have dict with max keys as first entry
    and min as the last
    """
    sorted_len = sorted(remove_null, key=len, reverse=True)

    for cred in sorted_len:
        if (('device_family' in cred and 'vendor' not in cred) or
            (('vendor' in cred) and
             ((cred['vendor'].lower() != mapped_value['vendor'].lower()) or
              ('device_family' in cred and cred['device_family'] not in
               mapped_value['family'])))):
            continue
        else:
            success = ssh_connect(
                ssh_conn,
                cred['credential']['username'],
                cred['credential']['password'],
                host,
                None,
                mapped_value)
            if success:
                return True
            else:
                logging.debug("Credentials didn't work for \
                              host {} with credentials {}".format(host),
                              cred['credential']['username'],
                              cred['credential']['password'])


def ping_sweep(host):
    try:
        ping_output = subprocess.Popen(['ping', '-c', '3', host],
                                       stdout=subprocess.PIPE)
        ping_output.wait()
        if ping_output.returncode == 0:
            return True
        else:
            return False
    except Exception as e:
        logging.debug("ERROR: SUBPROCESS.POPEN failed with error {}"
                      .format(str(e))
        return False


def get_device_vendor(oid):
    if "1.3.6.1.4.1.2636" in oid:
        return "Juniper"
    else:
        return ""


def oid_mapping_or_ssh(host, pysnmp_output, module):
    mapped_value={}
    device_info=module.params['device_family_info']
    if pysnmp_output['error']:
        logging.debug("SNMP failed for host {} with error {}".format(host,
                      pysnmp_output['error']))
        # find a credential that matches this host
        sshconn=paramiko.SSHClient()
        sshconn.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        for info in device_info:
            for cred in module.params['credentials']:
                success=ssh_connect(
                    sshconn,
                    cred['credential']['username'],
                    cred['credential']['password'],
                    host,
                    info['ssh_probe']['command'],
                    mapped_value)
                if success:
                    mapped_value['vendor']=info['vendor']
                    break
    else:
        vendor=get_device_vendor(pysnmp_output['ansible_sysobjectid'])
        if not vendor:
            logging.debug("Vendor {} for host {} not supported".format(vendor,
                           host))
        mapped_dict=next(element for element in device_info if element[
            'vendor'] == vendor)
        if mapped_dict:
            mapped_value=next(item for item in mapped_dict['snmp_probe'] if \
                              item['oid'] == \
                              pysnmp_output['ansible_sysobjectid'])
            mapped_value['hostname']=pysnmp_output['ansible_sysname']
            mapped_value['host']=host
            mapped_value['vendor']=vendor
    return mapped_value


def device_info_processing(host, module):
    result_return={}
    if ping_sweep(host):
        snmp_result=snmp_walk(host, module.params['version'], module.params[
             'community'])
        mapped_value=oid_mapping_or_ssh(host, snmp_result, module)
        if 'username' and 'password' not in mapped_value:
            ssh_check(host, module.params['credentials'], mapped_value)
        result_return=mapped_value.copy()
        return result_return
    else:
        logging.debug("HOST {}: NOT REACHABLE".format(host))


def main():
    module=AnsibleModule(
        argument_spec=dict(
            job_ctx=dict(type='dict', required=True),
            credentials=dict(required=True, type='list'),
            hosts=dict(type='list'),
            subnets=dict(type='list'),
            version=dict(required=True, choices=['v2', 'v2c', 'v3']),
            community=dict(required=True),
            device_family_info=dict(required=True, type='list')
        ),
        supports_check_mode=True,
        required_one_of=[['hosts', 'subnets']]
    )

    all_hosts=[]
    output=[]
    job_ctx=module.params['job_ctx']

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net=IPNetwork(subnet)
                all_hosts.extend(list(ip_net))
            except Exception as e:
                _result['failed']=True
                _result['msg']="ERROR: SUBNET NOT VALID " + subnet + \
                    "Failed with exception " + str(e)
                send_job_object_log(
                job_ctx, _result.get('msg'), JOB_IN_PROGRESS, None)
                module.exit_json(**_result)

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr=socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except Exception as e:
                _result['failed']=True
                _result['msg']="ERROR: HOST NOT VALID  " + \
                    host + "Failed with exception " + str(e)
                send_job_object_log(
                job_ctx, _result.get('msg'), JOB_IN_PROGRESS, None)
                module.exit_json(**_result)

    # Verify that we receive a community when using snmp v2
    if module.params['version'] == "v2" or module.params['version'] == "v2c":
        if module.params['community'] is None:
            _result['failed']=True
            _result['msg']="ERROR: Community not set when using snmp version 2"
            send_job_object_log(
                job_ctx, _result.get('msg'), JOB_IN_PROGRESS, None)
            module.exit_json(**_result)

    if module.params['version'] == "v3":
            _result['failed']=True
            _result['msg']="ERROR: Donot support snmp version 3"
            send_job_object_log(
                job_ctx, _result.get('msg'), JOB_IN_PROGRESS, None)
            module.exit_json(**_result)

    logging.basicConfig(filename="/tmp/ansible_out.log", level=logging.DEBUG)

    _result['msg']="Prefix(es) to be discovered: " + module.params['subnets']

    object_log = None
    try:
        object_log = ObjectLogUtil(job_ctx)
        object_log.send_job_object_log(
            _result.get('msg'), JOB_IN_PROGRESS, None)
    except ValueError as ve:
        logging.error(str(ve))
    except Exception as e:
        logging.error("Unable to log sandesh job logs: %s", str(e))
    finally:
        if object_log:
           object_log.close_sandesh_conn()

    threads=[Greenlet.spawn(device_info_processing,
                            str(host), module) for host in all_hosts]
    thread_result=gevent.joinall(threads)

    [output.append(thread.value) for thread in threads if \
        thread.value is not None]

    for device in output:
        _result['msg'] += "Device " + device.get('host') + "discovered " \
                          "with mapped info " + device.get('product') + \
                          ":" + device.get('family') + "\n"

    try:
        object_log.send_job_object_log(
            _result.get('msg'), JOB_IN_PROGRESS, None)
    except ValueError as ve:
        logging.error(str(ve))
    except Exception as e:
        logging.error("Unable to log sandesh job logs: %s", str(e))
    finally:
        if object_log:
           object_log.close_sandesh_conn()

    _result['device_info']=output
    module.exit_json(**_result)


if __name__ == '__main__':
    main()
