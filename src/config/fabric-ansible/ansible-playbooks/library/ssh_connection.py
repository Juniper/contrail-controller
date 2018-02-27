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

ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
Check for ssh connectivity with a given list of credentials against all rechables hosts.
If vendor and device family are specified, those credentials take precedence for a given host matching the specifics. 
'''
EXAMPLES = '''
ssh_connection:
          hosts: List of rechable hosts
          credentials: List of credentials
'''
RETURN = '''
List of hostnames long with their  valid credentials
'''

result = {'job_log_message': '', 'oid_mapping': {}}


def ssh_connect(ssh_conn, username, password, hostname):
    successful_cred = {}
    try:
        ssh_conn.connect(
            username=username,
            password=password,
            hostname=hostname)
        successful_cred.update(
            {'hostname': hostname, 'username': username, 'password': password})
        result['job_log_message'] += "\nTask: CREDENTIAL CHECK : " + \
            "Credentials worked for host: " + hostname
    except(paramiko.BadHostKeyException,
           paramiko.AuthenticationException,
           paramiko.SSHException,
           Exception) as e:
        result['job_log_message'] += "\nTask: CREDENTIAL CHECK: Host: " + \
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

    #If a dict value is None remove the key from the dict
    for single_dict in credentials:
        remove_null.append(
            dict([(dkey, ddata)
                 for dkey, ddata in single_dict.iteritems() if(ddata)]))
    #sort list of dict based on key len
    sorted_len = sorted(remove_null, key=len, reverse=True)

    for host in hosts:
        for cred in sorted_len:
            if ('vendor' in cred and cred['vendor'].lower() == host['vendor']):
                if 'device_family' in cred:
                    if (cred['device_family'] in host['family']):
                        valid_cred = ssh_connect(
                            ssh_conn,
                            cred['credential']['username'],
                            cred['credential']['password'],
                            host['host'])
                        if valid_cred:
                            successful_connections.append(valid_cred)
                        break
                    else:
                        continue
                valid_cred = ssh_connect(
                    ssh_conn,
                    cred['credential']['username'],
                    cred['credential']['password'],
                    host['host'])
                if valid_cred:
                    successful_connections.append(valid_cred)
                break
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

    module = AnsibleModule(
        argument_spec=dict(
            hosts=dict(required=True, type='list'),
            credentials=dict(required=True, type='list')
        ),
        supports_check_mode=True,
    )

    successful_connections = ssh_check(module)

    result['ssh_success'] = successful_connections
    module.exit_json(**result)


if __name__ == '__main__':
    main()
