#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""

import socket
from netaddr import IPNetwork
from gevent import Greenlet, monkey, pool
monkey.patch_all()
from ansible.module_utils.device_info import DeviceInfo
from ansible.module_utils.fabric_pysnmp import snmp_walk
from ansible.module_utils.fabric_utils import FabricAnsibleModule

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
                "data_format": "xml"
            },
            "vendor": "Juniper"
        }
    ]
'''

RETURN = '''

'''

JOB_IN_PROGRESS = "JOB_IN_PROGRESS"


def _single_greenlet_processing(host, deviceinfo):
    oid_mapped = {}
    logger = deviceinfo.logger

    if deviceinfo.ping_sweep(host):
        logger.info("HOST {}: REACHABLE".format(host))
        snmp_result = snmp_walk(host, '2vc', 'public')

        if snmp_result.get('error') is False:
            oid_mapped = deviceinfo.oid_mapping(host, snmp_result)
            if oid_mapped:
                logger.info("HOST {}: SNMP SUCCEEDED".format(host))
        else:
            logger.info(
                "SNMP failed for host {} with error {}".format(
                    host, snmp_result['error_msg']))

        if not oid_mapped or snmp_result.get('error') is True:
            deviceinfo.get_device_info_ssh(host, oid_mapped,
                                           deviceinfo.credentials)

        deviceinfo.device_info_processing(host, oid_mapped)
    else:
        logger.debug("HOST {}: NOT REACHABLE".format(host))
# end _device_info_processing


def _exit_with_error(module, msg):
    module.results['failed'] = True
    module.results['msg'] = msg
    module.send_job_object_log(module.results.get('msg'),
                               JOB_IN_PROGRESS, None)
    module.exit_json(**module.results)
# end _exit_with_error


def module_process(module):
    deviceinfo = DeviceInfo(module)
    concurrent = module.params['pool_size']

    all_hosts = []

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net = IPNetwork(subnet)
                all_hosts.extend(list(ip_net))
            except Exception as ex:
                _exit_with_error(module, "ERROR: Invalid subnet \"%s\" (%s)" %
                                 (subnet, str(ex)))

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr = socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except Exception as ex:
                _exit_with_error(
                    module, "ERROR: Invalid ip address \"%s\" (%s)" %
                    (host, str(ex)))

    if len(all_hosts) == 0:
        _exit_with_error(module, "NO HOSTS to DISCOVER")

    # Verify that we receive a community when using snmp v2
    if module.params['version'] == "v2" or module.params['version'] == "v2c":
        if module.params['community'] is None:
            _exit_with_error(module, "ERROR: Community not set when using \
                             snmp version 2")

    if module.params['version'] == "v3":
        _exit_with_error(module, "ERROR: Donot support snmp version 3")

    module.results['msg'] = "Prefix(es) to be discovered: " + \
        ','.join(module.params['subnets'])
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)

    if len(all_hosts) < concurrent:
        concurrent = len(all_hosts)

    deviceinfo.initial_processing(concurrent)

    threadpool = pool.Pool(concurrent)

    try:
        for host in all_hosts:
            threadpool.start(
                Greenlet(
                    _single_greenlet_processing,
                    str(host),
                    deviceinfo))
        threadpool.join()
    except Exception as ex:
        module.results['failed'] = True
        module.results['msg'] = "Greenlet spawn failed: %s"\
            % str(ex)
        module.exit_json(**module.results)

    module.results['device_info'] = DeviceInfo.output
    module.results['msg'] = "Device discovery complete"
    module.job_ctx['current_task_index'] = 3
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)
    module.exit_json(**module.results)
# end module_process


def main():
    """module main"""
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
            pool_size=dict(default=500, type='int'),
            total_retry_timeout=dict(default=0, type='int')
        ),
        supports_check_mode=True,
        required_one_of=[['hosts', 'subnets']]
    )

    module.execute(module_process)
# end main


if __name__ == '__main__':
    main()
