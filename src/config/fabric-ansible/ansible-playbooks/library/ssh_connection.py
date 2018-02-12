#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""
from ansible.module_utils.basic import AnsibleModule
import paramiko


__metaclass__ = type


ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
'''
EXAMPLES = '''
'''
RETURN = '''
'''


def ssh_check(module):
    hosts = module.params['hosts']
    credentials = module.params['credentials']

    successful_connections = []

    ssh_conn = paramiko.SSHClient()
    ssh_conn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    for host in hosts:
        for cred in credentials:
            try:
                ssh_conn.connect(username=cred['username'],
                                 password=cred['password'], hostname=host)
                successful_connections.append({'hostname': host,
                                               'username': cred['username'],
                                               'password': cred['password']})
                break
            except(paramiko.BadHostKeyException,
                   paramiko.AuthenticationException,
                   paramiko.SSHException, Exception) as e:
                continue

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

    result = {}
    result['ssh_success'] = successful_connections
    module.exit_json(**result)


if __name__ == '__main__':
    main()
