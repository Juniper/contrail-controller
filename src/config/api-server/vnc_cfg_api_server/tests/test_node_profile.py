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
from enum import Enum
from vnc_api.vnc_api import VncApi
import sys
import time
import base64

class NodeProfileStatus(Enum):
    INIT = 0
    IN_PROGRESS = 1
    COMPLETE = 2
    FAILED = 3
# end class JobStatus


class NodeProfileJob(object):
    JOB_STATUS_MAPPING = {
        'SUCCESS': NodeProfileStatus.COMPLETE,
        'FAILURE': NodeProfileStatus.FAILED,
        'UNKNOWN': NodeProfileStatus.FAILED
    }

    def __init__(self, job_type, job_input, api_server_config):
        self._job_type = job_type
        self._job_input = job_input
        self._api_server_config = api_server_config
        self._job_id = None
        self._job_status = NodeProfileStatus.INIT
        super(NodeProfileJob, self).__init__()
    # end __init__

    def push(self, timeout, max_retries):
        vnc_api = self._get_vnc_api(self._api_server_config)
        self._job_status = NodeProfileStatus.IN_PROGRESS
        job_execution_id = '123'
        try:
            print ("SD handler: executing job for (%s, %s)" %
                               (self._job_id, str(self._job_type)))
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
                               " execution id %s: %s" % (self._job_id,
                               str(self._job_type), job_execution_id, repr(e)))
            self._job_status = NodeProfileStatus.FAILED

        if self._job_status == NodeProfileStatus.FAILED:
            raise Exception("SD handler: push failed for (%s, %s)"
                            " execution id %s" % (self._job_id,
                            str(self._job_type), job_execution_id))
        print ("SD handler: push succeeded for (%s, %s)"
                            " execution id %s" % (self._job_id,
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
                                  NodeProfileStatus.COMPLETE):
            return NodeProfileStatus.COMPLETE
        if self._check_job_status(vnc_api, job_execution_id,
                                  NodeProfileStatus.FAILED):
            return NodeProfileStatus.FAILED

        return NodeProfileStatus.IN_PROGRESS
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
                        (job_execution_id, self._job_id,
                         str(self._job_type)))
                    self._job_status = NodeProfileStatus.FAILED
                else:
                    retry_count += 1
                    gevent.sleep(timeout)
    # end _wait

    def get_job_status(self):
        return self._job_status
    # end get_job_status

    def is_job_done(self):
        if self._job_status == NodeProfileStatus.COMPLETE or \
                self._job_status == NodeProfileStatus.FAILED:
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

    #file = open('node_profile.yaml', 'rb')
    file = open('node_profile.json', 'rb')
    file_read = file.read()
    file_base64_encode = base64.encodestring(file_read)

    fabric_fq_name = [
        'default-global-system-config', "abc"]
    cc_auth_host = "10.10.10.11"

    api_server_config = {
        'api_server_host': '10.10.10.10',
        'api_server_port': '8082',
        'username': 'admin',
        'password': 'password',
        'tenant_name': 'admin',
        'api_server_use_ssl': False
    }
    job_template_fq_name = [
        'default-global-system-config', 'node_profile_template']

    job_input = {
        "file_format": "yaml",
        "encoded_file": file_base64_encode,
        "contrail_command_host": cc_auth_host,
        "fabric_fq_name": fabric_fq_name
    }

    test_sd_obj = NodeProfileJob(job_template_fq_name,
                                  job_input,
                                  api_server_config=api_server_config)
    start_time = time.time()
    test_sd_obj.push(100, 5)
    end_time = time.time()

    print("time taken: " + str(end_time - start_time))

if __name__ == '__main__':
    sys.exit(main())
