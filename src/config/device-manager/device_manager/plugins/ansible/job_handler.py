#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of job api handler code
"""
import gevent
import json
from opserver_util import OpServerUtils
from enum import Enum


class JobType(Enum):
    UNDERLAY = 1
    OVERLAY = 2
# end class JobType


class JobStatus(Enum):
    INIT = 0
    IN_PROGRESS = 1
    COMPLETE = 2
    FAILED = 3
# end class JobStatus


class JobHandler(object):
    # job status query
    TABLE = 'ObjectJobExecutionTable'
    START_TIME = 'now-5m'
    END_TIME = 'now'
    FIELDS = ["MessageTS", "Messagetype"]
    OBJECT_ID = 'ObjectId'
    JOB_STATUS = {
        JobStatus.COMPLETE: "SUCCESS",
        JobStatus.FAILED: "FAILURE"
    }

    def __init__(self, job_type, job_input, device_list, analytics_config,
                 vnc_api, logger):
        self._job_type = job_type
        self._job_input = job_input
        self._device_list = device_list
        self._analytics_config = analytics_config
        self._vnc_api = vnc_api
        self._logger = logger
        self._job_id = None
        self._job_status = JobStatus.INIT
        super(JobHandler, self).__init__()
    # end __init__

    def push(self, timeout=15, max_retries=20):
        self._logger.info("job handler: push for (%s, %s): " %
                          (self._device_list, str(self._job_type)))
        self._job_status = JobStatus.IN_PROGRESS
        try:
            self._logger.debug("job handler: executing job for (%s, %s): " %
                               (self._device_list, str(self._job_type)))
            job_execution_info = self._vnc_api.execute_job(
                job_template_fq_name=self._job_type,
                job_input=self._job_input,
                device_list=self._device_list
            )

            job_execution_id = job_execution_info.get('job_execution_id')
            self._logger.debug("job started with execution id: %s" %
                               job_execution_id)
            self._wait(job_execution_id, timeout, max_retries)
        except Exception as e:
            self._logger.error("job handler: push failed for (%s, %s): %s" %
                               (self._device_list, str(self._job_type), str(e)))
            self._job_status = JobStatus.FAILED

        if self._job_status == JobStatus.FAILED:
            raise Exception("job handler: push failed for (%s, %s)" %
                            (self._device_list, str(self._job_type)))
    # end push

    @classmethod
    def _get_opserver_query(cls, job_execution_id, status):
        value = "%s:%s" % (job_execution_id, cls.JOB_STATUS.get(status.value))
        match = OpServerUtils.Match(name=cls.OBJECT_ID,
                                    value=value, op=OpServerUtils.MatchOp.EQUAL)
        return OpServerUtils.Query(cls.TABLE,
                                   start_time=cls.START_TIME,
                                   end_time=cls.END_TIME,
                                   select_fields=cls.FIELDS,
                                   where=[[match.__dict__]])
    # end _get_opserver_query

    def _check_job_status(self, url, job_execution_id, status):
        query = self._get_opserver_query(job_execution_id, status)
        username = self._analytics_config.get('username', None)
        password = self._analytics_config.get('password', None)
        resp = OpServerUtils.post_url_http(self._logger, url,
                                           query.__dict__,
                                           username, password)
        if resp is not None:
            resp = json.loads(resp)
            if resp and resp['value'] and len(resp['value']) > 0:
                return True
        else:
            self._logger.debug("job handler: invalid response for (%s, %s):" %
                               (self._device_list, str(self._job_type)))

        return False
    # end _check_job_status

    def _get_job_status(self, url, job_execution_id):
        if self._check_job_status(url, job_execution_id,
                                  JobStatus.COMPLETE):
            return JobStatus.COMPLETE
        if self._check_job_status(url, job_execution_id,
                                  JobStatus.FAILED):
            return JobStatus.FAILED

        return JobStatus.IN_PROGRESS
    # end _get_job_status

    def _wait(self, job_execution_id, timeout, max_retries):
        url = OpServerUtils.opserver_query_url(self._analytics_config['ip'],
                                               self._analytics_config['port'])

        retry_count = 1
        while not self.is_job_done():
            self._job_status = self._get_job_status(url, job_execution_id)
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
# end class JobHandler
