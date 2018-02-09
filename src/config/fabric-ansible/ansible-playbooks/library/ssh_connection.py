#!/usr/bin/python
from __future__ import absolute_import, division, print_function
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

from ansible.module_utils.basic import AnsibleModule
import paramiko

def ssh_check (module):
    hosts = module.params['hosts']
    credentials = module.params['credentials']

    successful_connections = []

    ssh_conn = paramiko.SSHClient()
    ssh_conn.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    for host in hosts:
        for cred in credentials:
            try:
                ssh_conn.connect(username=cred['username'], password=cred['password'], hostname=host)
                successful_connections.append({'hostname': host, 'username': cred['username'], 'password': cred['password']})
                break
            except(paramiko.BadHostKeyException, paramiko.AuthenticationException, paramiko.SSHException, Exception) as e:
                continue

    return successful_connections

def main():

    module = AnsibleModule(
        argument_spec=dict(
            hosts=dict(type='list'),
            credentials=dict(type='list')
        ),
        supports_check_mode=True,
    )

    successful_connections = ssh_check(module)

    result={}
    result['ssh_success'] = successful_connections
    module.exit_json(**result)


if __name__ == '__main__':
    main()
