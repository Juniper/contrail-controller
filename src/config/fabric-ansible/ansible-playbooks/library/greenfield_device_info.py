#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""
from datetime import datetime
import logging
logging.getLogger('requests.packages.urllib3.connectionpool').setLevel(logging.ERROR)
from gevent import Greenlet, monkey, pool, queue
monkey.patch_all()
from ansible.module_utils.device_info import DeviceInfo
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

def _single_greenlet_processing(host_params, deviceinfo, retry_queue):
    elapsed_time = 0
    logger = deviceinfo.logger

    if host_params.get('time') is not 0:
        current_time = datetime.now()
        time_diff = current_time - host_params.get('time')
        elapsed_time = time_diff.total_seconds()
        if host_params.get('time_duration') <= 0.0:
            logger.info("Host {} tried for the given retry timeout {}. "
                        "Discovery FAILED".format(host_params.get('host'),
                                                  deviceinfo.total_retry_timeout))
            return
        if elapsed_time < 30:
            retry_queue.put(host_params)
            return

        host_params['time_duration'] = host_params.get('time_duration') - elapsed_time
        logger.info("Retrying host {} after {}".format(host_params.get('host'),elapsed_time))

    if host_params.get('time') == 0 or elapsed_time >= 30:
        oid_mapped = {}
        success = False

        if deviceinfo.ping_sweep(host_params.get('host')):
            logger.info("HOST {}: REACHABLE".format(host_params.get('host')))
            success = deviceinfo.get_device_info_ssh(host_params.get('host'),
                                           oid_mapped, deviceinfo.credentials)
            if not success:
                logger.info("HOST {}: SSH UNSUCCESSFUL".format(host_params.get(
                    'host')))
                logger.info("Add in retry queue for host: {}".format(host_params.get('host')))
                host_params['time'] = datetime.now()
                retry_queue.put(host_params)
                return

            deviceinfo.device_info_processing(host_params.get('host'),
                                              oid_mapped)
        else:
            logger.info("HOST {}: NOT REACHABLE".format(host_params.get('host')))
            logger.info("Add in retry queue for host: "
                        "{}".format(host_params.get('host')))
            host_params['time'] = datetime.now()
            retry_queue.put(host_params)
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
    hosts = module.params['host_list']
    total_retry_timeout = float(module.params['total_retry_timeout'])

    all_hosts = []

    #retrieve the IP of the hosts
    for host in hosts:
        all_hosts.append(host.get("ip_addr"))

    if len(all_hosts) == 0:
        _exit_with_error(module, "NO HOSTS to DISCOVER")

    retry_queue = queue.Queue()

    for host in all_hosts:
        host_retry_map_dict = {'host': "", 'time': 0, 'time_duration': total_retry_timeout}
        host_retry_map_dict['host'] = host
        retry_queue.put(host_retry_map_dict)

    module.results['msg'] = "Hosts to be discovered: " + \
        ', '.join(all_hosts)
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)

    if len(all_hosts) < concurrent:
        concurrent = len(all_hosts)

    deviceinfo.initial_processing(concurrent)

    threadpool = pool.Pool(concurrent)

    while True:
        try:
            # for host in all_hosts:
            threadpool.start(
                Greenlet(
                    _single_greenlet_processing,
                    retry_queue.get_nowait(),
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
            host_list=dict(required=True, type=list),
            job_ctx=dict(type='dict', required=True),
            device_family_info=dict(required=True, type='list'),
            vendor_mapping=dict(required=True, type='list'),
            pool_size=dict(default=500, type='int'),
            total_retry_timeout=dict(default=3600, type='int')
        ),
        supports_check_mode=True
    )

    module.execute(module_process)
# end main


if __name__ == '__main__':
    main()
