#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Device discovery.

This file contains implementation of checking for successful SSH connections
"""

from builtins import range
from builtins import str
from datetime import datetime
import logging
import socket

from gevent import Greenlet, monkey, pool, queue
monkey.patch_all()
from ansible.module_utils.device_info import DeviceInfo # noqa
from ansible.module_utils.fabric_pysnmp import snmp_walk # noqa
from ansible.module_utils.fabric_utils import FabricAnsibleModule # noqa
from netaddr import IPNetwork

logging.getLogger(
    'requests.packages.urllib3.connectionpool').setLevel(logging.ERROR)

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


def _single_greenlet_processing(deviceinfo, retry_queue):
    elapsed_time = 0
    logger = deviceinfo.logger

    while True:
        try:
            host_params = retry_queue.get_nowait()

            # this is the time when 'host_params' is put into the retry_queue
            # after it fails the first time.
            if host_params.get('time') != 0:
                current_time = datetime.now()
                time_diff = current_time - host_params.get('time')
                elapsed_time = time_diff.total_seconds()
                if host_params.get('remaining_retry_duration') <= 0.0:
                    msg = "Host {} tried for the given retry timeout {}. " \
                          "Discovery FAILED" % (host_params.get('host'),
                                              deviceinfo.total_retry_timeout)
                    logger.info(msg)
                    deviceinfo.module.results['warning_msg'] += msg
                    deviceinfo.module.send_job_object_log(
                        msg, JOB_IN_PROGRESS, None)
                    return
                if elapsed_time < 30:
                    retry_queue.put(host_params)
                    return

                host_params['remaining_retry_duration'] = host_params.get(
                    'remaining_retry_duration') - elapsed_time
                logger.info(
                    "Retrying host {} after {}".format(
                        host_params.get('host'),
                        elapsed_time))

            if host_params.get('time') == 0 or elapsed_time >= 30:
                oid_mapped = {}
                if deviceinfo.ping_sweep(host_params.get('host')):
                    logger.info("HOST {}: REACHABLE".format(host_params.get(
                        'host')))
                    snmp_result = snmp_walk(host_params.get('host'), '2vc',
                                            'public')

                    if snmp_result.get('error') is False:
                        oid_mapped = deviceinfo.oid_mapping(
                            host_params.get('host'), snmp_result)
                        if oid_mapped:
                            logger.info(
                                "HOST {}: SNMP SUCCEEDED".format(
                                    host_params.get('host')))
                    else:
                        logger.info(
                            "SNMP failed for host {} with error {}".format(
                                host_params.get('host'), snmp_result[
                                    'error_msg']))

                    success = deviceinfo.get_device_info_ssh(
                        host_params.get('host'), oid_mapped,
                        deviceinfo.credentials)
                    if not success:
                        if deviceinfo.total_retry_timeout:
                            logger.info(
                                "HOST {}: SSH UNSUCCESSFUL. Add in retry "
                                "queue".format(
                                    host_params.get('host')))
                            host_params['time'] = datetime.now()
                            retry_queue.put(host_params)
                        else:
                            msg = "HOST {}: SSH UNSUCCESSFUL".format(
                                host_params.get('host'))
                            logger.info(msg)
                            deviceinfo.module.results['warning_msg'] += msg
                            deviceinfo.module.send_job_object_log(
                                msg, JOB_IN_PROGRESS, None)
                        return

                    deviceinfo.device_info_processing(host_params.get('host'),
                                                      oid_mapped)
                else:
                    if deviceinfo.total_retry_timeout:
                        logger.info(
                            "HOST {}: not reachable, "
                            "add in retry queue".format(
                                host_params.get('host')))
                        host_params['time'] = datetime.now()
                        retry_queue.put(host_params)
                    else:
                        msg = "HOST {}: NOT REACHABLE".format(host_params.get(
                            'host'))
                        logger.info(msg)
                        deviceinfo.module.results['warning_msg'] += msg
                        deviceinfo.module.send_job_object_log(
                            msg, JOB_IN_PROGRESS, None)

        except queue.Empty:
            logger.debug("QUEUE EMPTY EXIT")
            return
# end _single_greenlet_processing


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
    module.results['warning_msg'] = ''

    all_hosts = []

    # Verify that we receive a community when using snmp v2
    if module.params['version'] == "v2" or module.params['version'] == "v2c":
        if module.params['community'] is None:
            _exit_with_error(module, "ERROR: Community not set when using \
                             snmp version 2")

    if module.params['version'] == "v3":
        _exit_with_error(module, "ERROR: Donot support snmp version 3")

    if module.params['subnets']:
        for subnet in module.params['subnets']:
            try:
                ip_net = IPNetwork(subnet)
                all_hosts.extend(list(ip_net))
            except Exception as ex:
                _exit_with_error(module, "ERROR: Invalid subnet \"%s\" (%s)" %
                                 (subnet, str(ex)))
        module.results['msg'] = "Prefix(es) to be discovered: " + \
            ','.join(module.params['subnets'])
        module.send_job_object_log(
            module.results.get('msg'),
            JOB_IN_PROGRESS,
            None)

    if module.params['hosts']:
        for host in module.params['hosts']:
            try:
                ipaddr = socket.gethostbyname(host)
                all_hosts.append(ipaddr)
            except Exception as ex:
                _exit_with_error(
                    module, "ERROR: Invalid ip address \"%s\" (%s)" %
                    (host, str(ex)))

    if module.params['host_list']:
        for host in module.params['host_list']:
            all_hosts.append(host.get("ip_addr"))
        module.results['msg'] = "Hosts to be discovered: " + \
            ', '.join(all_hosts)
        module.send_job_object_log(
            module.results.get('msg'),
            JOB_IN_PROGRESS,
            None)

    deviceinfo.discovery_percentage_write()

    if len(all_hosts) == 0:
        _exit_with_error(module, "NO HOSTS to DISCOVER")

    # create a queue and all all hosts to the queue
    retry_queue = queue.Queue()

    for host in all_hosts:
        host_retry_map_dict = {
            'host': str(host),
            'time': 0,
            'remaining_retry_duration': deviceinfo.total_retry_timeout}
        retry_queue.put(host_retry_map_dict)

    if len(all_hosts) < concurrent:
        concurrent = len(all_hosts)

    deviceinfo.initial_processing(concurrent)

    threadpool = pool.Pool(concurrent)

    while True:
        try:
            if retry_queue.qsize() == 0:
                break
            for each_thread in range(concurrent):
                threadpool.start(
                    Greenlet(
                        _single_greenlet_processing,
                        deviceinfo,
                        retry_queue))
            threadpool.join()
        except queue.Empty:
            module.logger.info("QUEUE EMPTY EXIT")
            break
        except Exception as ex:
            module.results['failed'] = True
            module.results['msg'] = "Greenlet spawn failed: %s"\
                % str(ex)
            module.exit_json(**module.results)

    module.results['device_info'] = DeviceInfo.output
    if not module.results.get('device_info'):
        module.results['msg'] = "NO HOSTS DISCOVERED"
    else:
        module.results['msg'] = "Discovered " + str(len(module.results.get(
            'device_info'))) + " device(s)"
    module.job_ctx['current_task_index'] = 3
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)
    deviceinfo.discovery_percentage_write()
    module.exit_json(**module.results)
# end module_process


def main():
    module = FabricAnsibleModule(
        argument_spec=dict(
            fabric_uuid=dict(required=True),
            job_ctx=dict(type='dict', required=True),
            credentials=dict(type='list'),
            hosts=dict(type='list'),
            subnets=dict(type='list'),
            host_list=dict(type='list'),
            version=dict(required=True, choices=['v2', 'v2c', 'v3']),
            community=dict(required=True),
            device_family_info=dict(required=True, type='list'),
            vendor_mapping=dict(required=True, type='list'),
            pool_size=dict(default=500, type='int'),
            total_retry_timeout=dict(type='int')
        ),
        supports_check_mode=True,
        required_one_of=[['hosts', 'subnets', 'host_list']]
    )

    module.execute(module_process)
# end main


if __name__ == '__main__':
    main()
