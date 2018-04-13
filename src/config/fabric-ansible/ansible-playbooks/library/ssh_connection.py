#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""

__metaclass__ = type

from ansible.module_utils.fabric_utils import FabricAnsibleModule
import paramiko

ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
Check for ssh connectivity with a given list of credentials against all
rechables hosts.
If vendor and device family are specified, those credentials take precedence
for a given host matching the specifics.

ssh_connection:
          hosts: List of rechable hosts with vendor and family info
          credentials: List of credentials as given in the fabric object
'''

EXAMPLES = '''
ssh_connection:
          hosts:[{
                  "family": "juniper-mx",
                  "host": "1.15.6.1",
                  "hostname": "jtme-01",
                  "product": "m10i",
                  "vendor": "juniper"
                },
                {
                 "family": "juniper-mx",
                 "host": "1.15.6.2",
                 "hostname": "jtme-02",
                 "product": "m10i",
                 "vendor": "juniper"
                }]
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
'''

RETURN = '''
List of hostnames long with their  valid credentials
'''

_result = {'job_log_message': '', 'oid_mapping': {}}


def ssh_connect(ssh_conn, username, password, hostname):
    successful_cred = {}
    try:
        ssh_conn.connect(
            username=username,
            password=password,
            hostname=hostname)
        successful_cred.update(
            {'hostname': hostname, 'username': username, 'password': password})
        _result['job_log_message'] += "\nTask: CREDENTIAL CHECK : " + \
            "Credentials worked for host: " + hostname
    except(paramiko.BadHostKeyException,
           paramiko.AuthenticationException,
           paramiko.SSHException,
           Exception) as e:
        _result['job_log_message'] += "\nTask: CREDENTIAL CHECK: Host: " + \
            hostname + " hit the exception " + str(e)
        pass

    return successful_cred


def ssh_check(module):
    hosts = module.params['hosts']
    credentials = module.params['credentials']

    remove_null = []
    successful_connections = []
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

    for host in hosts:
        for cred in sorted_len:
            if (('device_family' in cred and 'vendor' not in cred) or
                (('vendor' in cred) and
                 ((cred['vendor'].lower() != host['vendor']) or
                  ('device_family' in cred and cred['device_family'] not in
                   host['family'])))):
                continue
            else:
                valid_cred = ssh_connect(
                    ssh_conn,
                    cred['credential']['username'],
                    cred['credential']['password'],
                    host['host'])
                if valid_cred:
                    successful_connections.append(valid_cred)
                    break

    return successful_connections


def main():

    module = FabricAnsibleModule(
        argument_spec=dict(
            hosts=dict(required=True, type='list'),
            credentials=dict(required=True, type='list')
        ),
        supports_check_mode=True,
    )

    successful_connections = ssh_check(module)

    _result['ssh_success'] = successful_connections
    module.exit_json(**_result)


if __name__ == '__main__':
    main()
