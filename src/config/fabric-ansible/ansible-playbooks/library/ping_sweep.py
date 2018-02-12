#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of sending ping requests to the list of
IP addresses defined by subnet
"""
from ansible.module_utils.basic import AnsibleModule
import subprocess
import ipaddress


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

result = {}

def check_ping(module):

    reachable = []
    unreachable = []

    try:
        if module.params['subnet']:
            ip_net = ipaddress.ip_network(module.params['subnet'])
            all_hosts = list(ip_net.hosts())
    except ValueError:
        result['msg'] = "Subnet is not valid"
        module.exit_json(**result)

    if module.params['hosts']:
        all_hosts = module.params['hosts']

    for host in all_hosts:
        ping_output = subprocess.Popen(['ping', '-c', '3', str(host)],
                                       stdout=subprocess.PIPE)
        ping_output.wait()

        if ping_output.returncode == 0:
            reachable.append(str(host))
        else:
            unreachable.append(str(host))

    return reachable, unreachable


def main():
    module = AnsibleModule(
        argument_spec=dict(
            hosts=dict(type='list'),
            subnet=dict()
        ),
        supports_check_mode=True,
        required_one_of=[['hosts', 'subnet']]
    )

    reachable, unreachable = check_ping(module)

    result['reachable_hosts'] = reachable
    result['unreachable_hosts'] = unreachable

    module.exit_json(**result)


if __name__ == '__main__':
    main()
