#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of job api handler code
"""
from __future__ import print_function

from builtins import str
from builtins import object
import gevent
import json
import random
from enum import Enum
from vnc_api.vnc_api import VncApi
import sys
import time


class ServerDiscoveryStatus(Enum):
    INIT = 0
    IN_PROGRESS = 1
    COMPLETE = 2
    FAILED = 3
# end class JobStatus


class ServerDiscoveryJob(object):
    JOB_STATUS_MAPPING = {
        'SUCCESS': ServerDiscoveryStatus.COMPLETE,
        'FAILURE': ServerDiscoveryStatus.FAILED,
        'UNKNOWN': ServerDiscoveryStatus.FAILED
    }

    def __init__(self, job_type, job_input, api_server_config):
        self._job_type = job_type
        self._job_input = job_input
        self._api_server_config = api_server_config
        self._job_id = None
        self._job_status = ServerDiscoveryStatus.INIT
        self._ipmi_subnet_list = job_input['ipmi']['ipmi_subnet_list']
        super(ServerDiscoveryJob, self).__init__()
    # end __init__

    def push(self, timeout, max_retries):
        vnc_api = self._get_vnc_api(self._api_server_config)
        self._job_status = ServerDiscoveryStatus.IN_PROGRESS
        job_execution_id = '123'
        try:
            print ("SD handler: executing job for (%s, %s)" %
                               (self._ipmi_subnet_list, str(self._job_type)))
            #import pdb; pdb.set_trace()
            job_execution_info = vnc_api.execute_job(
                job_template_fq_name=self._job_type,
                job_input=self._job_input
            )

            job_execution_id = job_execution_info.get('job_execution_id')
            print ("SD started with execution id %s" %
                               job_execution_id)
            self._wait(vnc_api, job_execution_id, timeout, max_retries)
        except Exception as e:
            print ("SD handler: push failed for (%s, %s)"
                               " execution id %s: %s" % (self._ipmi_subnet_list,
                               str(self._job_type), job_execution_id, repr(e)))
            self._job_status = ServerDiscoveryStatus.FAILED

        if self._job_status == ServerDiscoveryStatus.FAILED:
            raise Exception("SD handler: push failed for (%s, %s)"
                            " execution id %s" % (self._ipmi_subnet_list,
                            str(self._job_type), job_execution_id))
        print ("SD handler: push succeeded for (%s, %s)"
                            " execution id %s" % (self._ipmi_subnet_list,
                            str(self._job_type), job_execution_id))
    # end push

    def _check_job_status(self, vnc_api, job_execution_id, status):
        try:
            job_status = vnc_api.job_status(job_execution_id)
            return self._verify_job_status(job_status, status)
        except Exception as e:
            print ("SD handler: error while querying "
                "SD status for execution_id %s: %s" %
                (job_execution_id, repr(e)))
        return False
    # end _check_job_status

    def _get_job_status(self, vnc_api, job_execution_id):
        if self._check_job_status(vnc_api, job_execution_id,
                                  ServerDiscoveryStatus.COMPLETE):
            return ServerDiscoveryStatus.COMPLETE
        if self._check_job_status(vnc_api, job_execution_id,
                                  ServerDiscoveryStatus.FAILED):
            return ServerDiscoveryStatus.FAILED

        return ServerDiscoveryStatus.IN_PROGRESS
    # end _get_job_status

    def _wait(self, vnc_api, job_execution_id, timeout, max_retries):
        retry_count = 1
        while not self.is_job_done():
            self._job_status = self._get_job_status(vnc_api, job_execution_id)
            if not self.is_job_done():
                if retry_count >= max_retries:
                    print (
                        "SD handler: timed out waiting for job %s for device"
                        " %s and job_type %s:" %
                        (job_execution_id, self._ipmi_subnet_list,
                         str(self._job_type)))
                    self._job_status = ServerDiscoveryStatus.FAILED
                else:
                    retry_count += 1
                    gevent.sleep(timeout)
    # end _wait

    def get_job_status(self):
        return self._job_status
    # end get_job_status

    def is_job_done(self):
        if self._job_status == ServerDiscoveryStatus.COMPLETE or \
                self._job_status == ServerDiscoveryStatus.FAILED:
            return True
        return False
    # end is_job_done

    @staticmethod
    def _get_vnc_api(api_config):
        return VncApi(
            api_server_host=api_config.get('api_server_host'),
            api_server_port=api_config.get('api_server_port'),
            username=api_config.get('username'),
            password=api_config.get('password'),
            tenant_name=api_config.get('tenant_name'),
            api_server_use_ssl=api_config.get('api_server_use_ssl'))
    # end _get_vnc_api

    @classmethod
    def _verify_job_status(cls, job_status, status):
        return job_status and \
            cls.JOB_STATUS_MAPPING.get(job_status.get('job_status')) == \
                status
    # end _verify_job_status
# end class JobHandler

def main():
    """Main entry point for the script."""
    fabric_fq_name = [
        'default-global-system-config', "abc"]
    ipmi_config = {
        "ipmi_subnet_list": [
            "10.10.10.10/32",
            "20.20.20.0/24"
        ],
        "ipmi_credentials": [
            "admin:admin",
            "admin:password",
            "ADMIN:ADMIN"
        ],
        "ipmi_port_ranges": [
            {
                "port_range_start": 623,
                "port_range_end": 623
            }
        ]
    }
    ironic_config = {
        "auth_url": "http://1.1.1.1:5000/v3",
        "username": "admin",
        "password": "password"
    }
    cc_auth_host = "10.10.10.11"
    job_input = {
        "ipmi": ipmi_config,
        "ironic": ironic_config,
        "contrail_command_host": cc_auth_host,
        "fabric_fq_name": fabric_fq_name
    }
    job_template_fq_name = [
        'default-global-system-config', 'discover_server_template']

    api_server_config = {
        'api_server_host': '1.1.1.3',
        'api_server_port': '8082',
        'username': 'admin',
        'password': 'password',
        'tenant_name': 'admin',
        'api_server_use_ssl': False
    }
    test_sd_obj = ServerDiscoveryJob(job_template_fq_name,
                                     job_input,
                                     api_server_config=api_server_config
                                     )
    start_time = time.time()
    test_sd_obj.push(30,5)
    end_time = time.time()

    print("time taken: " + str(end_time - start_time))

if __name__ == '__main__':
    sys.exit(main())

