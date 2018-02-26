#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used by the job manager
"""

from enum import Enum
from time import gmtime, strftime
import traceback

from job_exception import JobException


class JobStatus(Enum):
    STARTING = 0
    IN_PROGRESS = 1
    SUCCESS = 2
    FAILURE = 3
    TIMEOUT = 4


class JobUtils(object):

    def __init__(self, job_execution_id, job_template_id, logger, vnc_api):
        self._job_execution_id = job_execution_id
        self._job_template_id = job_template_id
        self._logger = logger
        self._vnc_api = vnc_api

    def read_job_template(self):
        try:
            job_template = self._vnc_api.job_template_read(
                id=self._job_template_id)
            self._logger.debug("Read job template %s from "
                               "database" % self._job_template_id)
        except Exception as e:
            msg = "Error while reading the job template %s from " \
                  "database" % self._job_template_id
            raise JobException(msg, self._job_execution_id)
        return job_template

    def get_device_family(self, device_id):
        try:
            pr = self._vnc_api.physical_router_read(id=device_id)
            self._logger.debug("Read device family as %s for device "
                               "%s" % (pr.get_physical_router_device_family(),
                                       device_id))
        except Exception as e:
            msg = "Error while reading the device family from DB for " \
                  "device %s " % device_id
            raise JobException(msg, self._job_execution_id)

        return pr.get_physical_router_device_family()
