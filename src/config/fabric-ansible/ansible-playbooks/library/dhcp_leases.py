#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains code to gather entries from the DHCP lease table and form a list
of device addresses for use in ZTP
"""
import re
import time
from netaddr import IPAddress, IPNetwork
from ansible.module_utils.basic import AnsibleModule

ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
module: dhcp_lease
author: Juniper Networks
short_description: Return a list of IP addresses of connected devices.
description:
    - Private module to read from DHCP lease table and return a list
    of IP addresses of connected devices within the configured IPAM.
    The number of expected devices is passed in as an argument.
requirements:
    - DHCP server must be configured and active
options:
    device_count:
        description:
            - Number of devices to gather information. Module will wait <timeout>
              seconds for this many devices to come up. IP addresses are read from
              the DHCP lease table
        required: true
    ztp_config:
        description:
            - ZTP configuration containing IPAM configuration. Used to verify
              IP addresses fall within one of the subnets defined in the IPAM.
        required: true
    timeout:
        description:
            - Number of seconds to wait for devices to come up. Default 300 sec.
        required: false
    file_path:
        description:
            - Path to the location of the dhcp leases file. Defaults to
              /var/lib/dhcpd/dhcpd.leases
        required: false
'''

EXAMPLES = '''
- name: Get list of device IPs
    hosts: dhcp
    gather_facts: no
    tasks:
        - dhcp_leases:
            device_count: "{{hostvars['localhost']['device_count']}}"
            ztp_config: "{{hostvars['localhost']['ztp_config']}}"
            timeout: "{{hostvars['localhost']['dhcp_boot_wait']}}"
          register: lease_res
        - name: print lease results
          debug: var=lease_res verbosity=1
'''

RETURN = '''
device_list:
  description:
    - A list of IP/MAC address pairs. Only those devices falling within the IPAM
      subnets are returned
  returned: on success always
  type: list

msg:
  description:
    - Message detailing how many devices were expected and found
  returned: always
  type: str
'''

DEFAULT_TIMEOUT = 300
DEFAULT_DHCPD_LEASES_PATH = "/var/lib/dhcpd/dhcpd.leases"


# Check whether an IP host address is within the list of subnets configured in the IPAM
def within_dhcp_subnet(ip_addr, ztp_config):
    if 'ipam_subnets' not in ztp_config:
        return False

    subnets = ztp_config['ipam_subnets']
    for sub in subnets:
        subnet = sub['subnet']
        ip_prefix = subnet['ip_prefix']
        length = subnet['ip_prefix_len']
        if IPAddress(ip_addr) in IPNetwork("{}/{}".format(ip_prefix, length)):
            return True
    return False


# Process the entries from the DHCP lease table
def process_dhcp_entries(module):
    results = {}
    results['failed'] = True
    pattern = re.compile(r"lease ([0-9.]+) {.*?hardware ethernet ([:a-f0-9]+);.*?}",
                         re.MULTILINE | re.DOTALL)

    device_count = module.params['device_count']
    ztp_config = module.params['ztp_config']
    timeout = module.params['timeout']
    dhcp_leases_path = module.params['file_path']

    while timeout > 0:
        timeout -= 1
        results['device_list'] = []
        lease_table = {}

        # Read DHCP lease database and store by mac. Note that there may be
        # multiple IPs per mac, but the latest one overwrites older entries
        with open(dhcp_leases_path) as lfile:
            for match in pattern.finditer(lfile.read()):
                ip_addr = match.group(1)
                mac = match.group(2)
                lease_table[mac] = ip_addr

        # Now verify that the IP address is inside one of the subnets from the IPAM
        # and store IP/MAC entries in a simple list
        for mac, ip_addr in lease_table.iteritems():
            if within_dhcp_subnet(ip_addr, ztp_config):
                results['device_list'].append({"ip_addr": ip_addr, "mac": mac})

        # If we found at least as many devices as expected, then we are done
        if len(results['device_list']) >= device_count:
            results['failed'] = False
            break

        # Not done, so wait a second and try again
        time.sleep(1)

    results['msg'] = "Found {} devices, expected {} devices".\
	format(len(results['device_list']), device_count)
    return results


def main():
    module = AnsibleModule(
        argument_spec=dict(
            device_count=dict(type=int, required=True),
            ztp_config=dict(type=dict, required=True),
            timeout=dict(type=int, required=False, default=DEFAULT_TIMEOUT),
            file_path=dict(type=str, required=False,
                           default=DEFAULT_DHCPD_LEASES_PATH)
            ),
        supports_check_mode=False)

    results = process_dhcp_entries(module)

    if results['failed'] is True:
        module.fail_json(**results)
    else:
        module.exit_json(**results)


if __name__ == '__main__':
    main()
