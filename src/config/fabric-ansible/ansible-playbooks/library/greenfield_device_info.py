#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of checking for successful SSH connections
"""
from datetime import datetime
from vnc_api.vnc_api import VncApi
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

REF_EXISTS_ERROR = 3
JOB_IN_PROGRESS = 1


def _device_info_processing(host_params, vncapi, module, fabric_uuid, retry_queue):
    elapsed_time = 0
    logger = module.logger
    total_retry_timeout = float(module.params['total_retry_timeout'])

    if host_params.get('time') is not 0:
        current_time = datetime.now()
        time_diff = current_time - host_params.get('time')
        elapsed_time = time_diff.total_seconds()
        if host_params.get('time_duration') <= 0.0:
            logger.info("Host {} tried for the given retry timeout {}. "
                        "Discovery FAILED".format(host_params.get('host'),
                                                  total_retry_timeout))
            return
        if elapsed_time < 30:
            retry_queue.put(host_params)
            return

        host_params['time_duration'] = host_params.get('time_duration') - elapsed_time
        logger.info("Retrying host {} after {}".format(host_params.get('host'),elapsed_time))

    if host_params.get('time') == 0 or elapsed_time >= 30:
        oid_mapped = {}
        valid_creds = False
        return_code = True
        success = False
        serial_num_flag = False
        serial_num = []
        all_serial_num = []
        deviceinfo = DeviceInfo(module)

        #get device credentials
        fabric = vncapi.fabric_read(id=fabric_uuid)
        fabric_object = vncapi.obj_to_dict(fabric)
        credentials = fabric_object.get('fabric_credentials').get('device_credential')

        #get serial numbers
        fabric_namespace_obj_list = vncapi.fabric_namespaces_list(
            parent_id=fabric_uuid, detail=True)
        fabric_namespace_list = vncapi.obj_to_dict(fabric_namespace_obj_list)

        for namespace in fabric_namespace_list:
            if namespace.get('fabric_namespace_type') == "SERIAL_NUM":
                serial_num_flag = True
                serial_num.append(namespace.get('fabric_namespace_value').get('serial_num'))

        if len(serial_num) > 1:
            for outer_list in serial_num:
                for sn in outer_list:
                    all_serial_num.append(sn)

        if deviceinfo.ping_sweep(host_params.get('host')):
            logger.info("HOST {}: REACHABLE".format(host_params.get('host')))
            success = deviceinfo.get_device_info_ssh(host_params.get('host'),
                                           oid_mapped, credentials)
            if not success:
                logger.info("HOST {}: SSH UNSUCCESSFUL".format(host_params.get(
                    'host')))
                logger.info("Add in retry queue for host: {}".format(host_params.get('host')))
                host_params['time'] = datetime.now()
                retry_queue.put(host_params)
                return
            if not oid_mapped.get('family') or not oid_mapped.get('vendor'):
                logger.info("Could not retrieve family/vendor info for the \
                    host: {}, not creating PR object".format(host_params.get('host')))
                logger.info("vendor: {}, family: {}".format(
                    oid_mapped.get('vendor'), oid_mapped.get('family')))
                oid_mapped = {}

            if oid_mapped.get('host'):
                valid_creds = deviceinfo.detailed_cred_check(host_params.get('host'), oid_mapped,
                                                             credentials)

            if not valid_creds and oid_mapped:
                logger.info("No credentials matched for host: {}, nothing to "
                            "update in DB".format(host_params.get('host')))
                oid_mapped = {}

            if oid_mapped:
                if serial_num_flag:
                    if oid_mapped.get('serial-number') not in all_serial_num:
                        logger.info("Serial number {} for host {} not present "
                                    "in fabric_namespace, nothing to "
                                    "update in DB".format(
                            oid_mapped.get('serial-number'),
                            host_params.get('host')))
                        return
                fq_name = [
                    'default-global-system-config',
                    oid_mapped.get('hostname')]
                return_code, pr_uuid = deviceinfo.pr_object_create_update(
                    vncapi, oid_mapped, fq_name, False)
                if return_code == REF_EXISTS_ERROR:
                    physicalrouter = vncapi.physical_router_read(
                        fq_name=fq_name)
                    phy_router = vncapi.obj_to_dict(physicalrouter)
                    if (phy_router.get('physical_router_management_ip')
                            == oid_mapped.get('host')):
                        logger.info(
                            "Device with same mgmt ip already exists {}".format(
                                phy_router.get('physical_router_management_ip')))
                        return_code, pr_uuid = deviceinfo.pr_object_create_update(
                            vncapi, oid_mapped, fq_name, True)
                    else:
                        fq_name = [
                            'default-global-system-config',
                            oid_mapped.get('hostname') +
                            '_' +
                            oid_mapped.get('host')]
                        return_code, pr_uuid = deviceinfo.pr_object_create_update(
                            vncapi, oid_mapped, fq_name, False)
                        if return_code == REF_EXISTS_ERROR:
                            logger.debug("Object already exists")
                if return_code is True:
                    vncapi.ref_update(
                        "fabric", fabric_uuid, "physical_router", pr_uuid, None, "ADD")
                    logger.info("Fabric updated with physical router info for host: {}".format(host_params.get('host')))
                    temp = {}
                    temp['device_management_ip'] = oid_mapped['host']
                    temp['device_fqname'] = fq_name
                    temp['device_username'] =  oid_mapped['username']
                    temp['device_password'] = oid_mapped['password']
                    temp['device_family'] = oid_mapped['family']
                    temp['device_vendor'] = oid_mapped['vendor']
                    DeviceInfo.output.update({pr_uuid:temp})
        else:
            logger.info("HOST {}: NOT REACHABLE".format(host_params.get('host')))
            logger.info("Add in retry queue for host: {}".format(host_params.get('host')))
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
    concurrent = module.params['pool_size']
    fabric_uuid = module.params['fabric_uuid']
    all_hosts = module.params['host_list']
    total_retry_timeout = float(module.params['total_retry_timeout'])

    module.results['msg'] = "Hosts to be discovered: " + \
        ','.join(module.params['host_list'])
    module.send_job_object_log(
        module.results.get('msg'),
        JOB_IN_PROGRESS,
        None)

    retry_queue = queue.Queue()

    if len(all_hosts) < concurrent:
        concurrent = len(all_hosts)

    threadpool = pool.Pool(concurrent)

    for host in all_hosts:
        host_retry_map_dict = {'host': "", 'time': 0, 'time_duration': total_retry_timeout}
        host_retry_map_dict['host'] = host
        retry_queue.put(host_retry_map_dict)

    while True:
        try:
            vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                            auth_token=module.job_ctx.get('auth_token'))
            # for host in all_hosts:
            threadpool.start(
                Greenlet(
                    _device_info_processing,
                    retry_queue.get_nowait(),
                    vncapi,
                    module,
                    fabric_uuid,
                    retry_queue))
            threadpool.join()
        except queue.Empty:
            module.logger.info("QUEUE EMPTY EXIT")
            break
        except Exception as ex:
            module.results['failed'] = True
            module.results['msg'] = "Failed to connect to API server due to error: %s"\
                % str(ex)
            module.exit_json(**module.results)

    module.results['output'] = DeviceInfo.output
    module.results['msg'] = "Device discovery complete"
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
