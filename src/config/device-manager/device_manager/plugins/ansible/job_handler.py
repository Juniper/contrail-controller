#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of job api handler code
"""
import gevent
import json
import random
from enum import Enum
from vnc_api.vnc_api import VncApi


class JobStatus(Enum):
    INIT = 0
    IN_PROGRESS = 1
    COMPLETE = 2
    FAILED = 3
# end class JobStatus


class JobHandler(object):
    JOB_STATUS_MAPPING = {
        'SUCCESS': JobStatus.COMPLETE,
        'FAILURE': JobStatus.FAILED,
        'UNKNOWN': JobStatus.FAILED
    }

    def __init__(self, job_type, job_input, device_list, api_server_config,
                 logger):
        self._job_type = job_type
        self._job_input = job_input
        self._device_list = device_list
        self._api_server_config = api_server_config
        self._logger = logger
        self._job_id = None
        self._job_status = JobStatus.INIT
        super(JobHandler, self).__init__()
    # end __init__

    def push(self, timeout, max_retries):
        vnc_api = self._get_vnc_api(**self._api_server_config)
        self._job_status = JobStatus.IN_PROGRESS
        try:
            self._logger.debug("job handler: executing job for (%s, %s)" %
                               (self._device_list, str(self._job_type)))
            job_execution_info = vnc_api.execute_job(
                job_template_fq_name=self._job_type,
                job_input=self._job_input,
                device_list=self._device_list
            )

            job_execution_id = job_execution_info.get('job_execution_id')
            self._logger.debug("job started with execution id: %s" %
                               job_execution_id)
            self._wait(vnc_api, job_execution_id, timeout, max_retries)
            self._logger.debug("job handler: push succeeded for (%s, %s)" %
                               (self._device_list, str(self._job_type)))
        except Exception as e:
            self._logger.error("job handler: push failed for (%s, %s): %s" %
                               (self._device_list, str(self._job_type), str(e)))
            self._job_status = JobStatus.FAILED

        if self._job_status == JobStatus.FAILED:
            raise Exception("job handler: push failed for (%s, %s)" %
                            (self._device_list, str(self._job_type)))
    # end push

    def _check_job_status(self, vnc_api, job_execution_id, status):
        try:
            job_status = vnc_api.job_status(job_execution_id)
            return self._verify_job_status(job_status, status)
        except Exception as e:
            self._logger.error("job handler: error while querying "
                "job status for execution_id %s: %s" %
                (job_execution_id, str(e)))
        return False
    # end _check_job_status

    def _get_job_status(self, vnc_api, job_execution_id):
        if self._check_job_status(vnc_api, job_execution_id,
                                  JobStatus.COMPLETE):
            return JobStatus.COMPLETE
        if self._check_job_status(vnc_api, job_execution_id,
                                  JobStatus.FAILED):
            return JobStatus.FAILED

        return JobStatus.IN_PROGRESS
    # end _get_job_status

    def _wait(self, vnc_api, job_execution_id, timeout, max_retries):
        retry_count = 1
        while not self.is_job_done():
            self._job_status = self._get_job_status(vnc_api, job_execution_id)
            if not self.is_job_done():
                if retry_count >= max_retries:
                    self._logger.error(
                        "job handler: timed out waiting for job %s for device"
                        " %s and job_type %s:" %
                        (job_execution_id, self._device_list,
                         str(self._job_type)))
                    self._job_status = JobStatus.FAILED
                else:
                    retry_count += 1
                    gevent.sleep(timeout)
    # end _wait

    def get_job_status(self):
        return self._job_status
    # end get_job_status

    def is_job_done(self):
        if self._job_status == JobStatus.COMPLETE or \
                self._job_status == JobStatus.FAILED:
            return True
        return False
    # end is_job_done

    @staticmethod
    def _get_vnc_api(ips, port, username, password, tenant, use_ssl):
        return VncApi(api_server_host=random.choice(ips),
            api_server_port=port, username=username,
            password=password, tenant_name=tenant,
            api_server_use_ssl=use_ssl)
    # end _get_vnc_api

    @staticmethod
    def _verify_job_status(job_status, status):
        return job_status and \
            self.JOB_STATUS_MAPPING.get(job_status.get('job_status')) == \
                status
    # end _verify_job_status
# end class JobHandler
