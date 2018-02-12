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
import socket
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
    all_hosts = []

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net = ipaddress.ip_network(unicode(subnet))
                all_hosts.extend(list(ip_net.hosts()))
            except ValueError:
                result['failure'] = "Subnet is not valid " + subnet
                module.exit_json(**result)

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr = socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except (ValueError, Exception) as e:
                result['failure'] = "Host not valid " + \
                    host + "Failed with exception " + str(e)
                module.exit_json(**result)

    for ip in all_hosts:
        ping_output = subprocess.Popen(['ping', '-c', '3', str(ip)],
                                       stdout=subprocess.PIPE)
        ping_output.wait()
        if ping_output.returncode == 0:
            reachable.append(str(ip))
        else:
            unreachable.append(str(ip))

    return reachable, unreachable


def main():
    module = AnsibleModule(
        argument_spec=dict(
            hosts=dict(type='list'),
            subnets=dict(type='list')
        ),
        supports_check_mode=True,
        required_one_of=[['hosts', 'subnets']]
    )

    reachable, unreachable = check_ping(module)

    result['reachable_hosts'] = reachable
    result['unreachable_hosts'] = unreachable
    result['job_log_message'] = "Task: PING SWEEP: ping sweep completed." + \
                                "Reachable hosts are : " + ','.join(reachable)

    module.exit_json(**result)


if __name__ == '__main__':
    main()
