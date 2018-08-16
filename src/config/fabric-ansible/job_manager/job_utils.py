#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used by the job manager
"""
import random
from enum import Enum
import traceback

from vnc_api.vnc_api import VncApi
import vnc_api

from job_exception import JobException
from job_messages import MsgBundle
from inflection import camelize


class JobStatus(Enum):
    STARTING = "STARTING"
    IN_PROGRESS = "IN_PROGRESS"
    SUCCESS = "SUCCESS"
    FAILURE = "FAILURE"

class JobVncApi(object):
    @staticmethod
    def vnc_init(job_ctx):
        host = random.choice(job_ctx.get('api_server_host'))
        return VncApi(
            api_server_host=host,
            auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
            auth_token=job_ctx.get('auth_token')
        )

    @staticmethod
    def get_vnc_cls(object_type):
        cls_name = camelize(object_type)
        return getattr(vnc_api.gen.resource_client, cls_name, None)

class JobFileWrite(object):
    JOB_PROGRESS = 'JOB_PROGRESS##'
    PLAYBOOK_OUTPUT = 'PLAYBOOK_OUTPUT##'
    JOB_LOG = 'JOB_LOG##'
    PROUTER_LOG = 'PROUTER_LOG##'
    GEN_DEV_OP_RES = 'GENERIC_DEVICE##'

    def __init__(self, logger, ):
        self._logger = logger

    def write_to_file(self, exec_id, pb_id, marker, msg):
        try:
            fname = '/tmp/%s' % str(exec_id)
            with open(fname, "a") as f:
                line_in_file = "%s%s%s%s\n" % (
                    str(pb_id), marker, msg, marker)
                f.write(line_in_file)
        except Exception as ex:
            self._logger.error("Failed to write_to_file: %s\n%s\n" % (
                str(ex), traceback.format_exc()
            ))


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
            msg = MsgBundle.getMessage(MsgBundle.READ_JOB_TEMPLATE_ERROR,
                                       job_template_id=self._job_template_id)
            raise JobException(msg, self._job_execution_id)
        return job_template

