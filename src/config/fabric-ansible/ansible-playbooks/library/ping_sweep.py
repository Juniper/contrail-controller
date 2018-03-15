#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of sending ping requests to the list of
IP addresses defined by subnet
"""

__metaclass__ = type

from ansible.module_utils.basic import AnsibleModule
import subprocess
import socket
from netaddr import IPNetwork

ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
For a given subnet or a list of IPs/hosts, check if they are reachable through
ICMP ping.

ping_sweep:
        subnets: List of IP Prefixes
        hosts: List of Ip addresses / hostnames
'''

EXAMPLES = '''
ping_sweep:
        subnets: ["10.155.67.0/29","10.155.72.0/30"]
OR

ping_sweep:
        hosts: ["10.155.68.2", "10.102.65.4", "10.157.3.2"]

'''

RETURN = '''
Return two lists, rechable hosts and unrechable hosts.
'''

_result = {}


def check_ping(module):
    reachable = []
    unreachable = []
    all_hosts = []
    _result['failed'] = False

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net = IPNetwork(subnet)
                all_hosts.extend(list(ip_net))
            except Exception as e:
                _result['failed'] = True
                _result['msg'] = "Subnet not valid " + subnet + \
                    "Failed with exception " + str(e)
                module.exit_json(**_result)

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr = socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except Exception as e:
                _result['failed'] = True
                _result['msg'] = "Host not valid " + \
                    host + "Failed with exception " + str(e)
                module.exit_json(**_result)

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

    _result['reachable_hosts'] = reachable
    _result['unreachable_hosts'] = unreachable
    _result['job_log_message'] = "Task: PING SWEEP: ping sweep completed." + \
        "Reachable hosts are : " + ','.join(reachable)

    module.exit_json(**_result)


if __name__ == '__main__':
    main()
