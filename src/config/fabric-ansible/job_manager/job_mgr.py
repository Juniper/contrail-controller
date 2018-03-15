#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager process code and api
"""
from gevent import monkey
monkey.patch_socket()
from gevent.pool import Pool
from time import gmtime, strftime
from gevent.greenlet import Greenlet
import sys
import json
import argparse
import traceback

from vnc_api.vnc_api import *

from job_handler import JobHandler
from job_exception import JobException
from job_result_handler import JobResultHandler
from logger import JobLogger
from job_utils import JobStatus, JobUtils

POOL_SIZE = 20

# TODO add api for use by the module callbacks


class JobManager(object):

    def __init__(self, logger, vnc_api, job_input):
        self._logger = logger
        self._vnc_api = vnc_api
        self.job_template_id = None
        self.job_execution_id = None
        self.job_data = None
        self.job_params = dict()
        self.device_json = None
        self.auth_token = None        
        self.tenant_name = None
        self.parse_job_input(job_input)
        self.job_utils = JobUtils(self.job_execution_id, self.job_template_id,
                                  self._logger, self._vnc_api)
        logger.debug("Job manager initialized")

    def parse_job_input(self, job_input_json):
        # job input should have job_template_id and execution_id field
        if job_input_json.get('job_template_id') is None:
            raise Exception("job_template_id is missing in the job input")

        if job_input_json.get('job_execution_id') is None:
            raise Exception("job_execution_id is missing in the job input")

        self.job_template_id = job_input_json['job_template_id']
        self.job_execution_id = job_input_json['job_execution_id']

        self.job_data = job_input_json.get('input')
        if self.job_data is None:
            self._logger.debug("Job input data is not provided.")

        self.job_params = job_input_json.get('params')
        if self.job_params is None:
            self._logger.debug("Job extra params is not present.")

        self.device_json = job_input_json.get('device_json')
        if self.device_json is None:
            self._logger.debug("Device data is not passed from api server.")

        self.auth_token = job_input_json['auth_token']
        self.tenant_name = job_input_json['tenant_name']

    def start_job(self):
        try:
            # create job UVE and log
            msg = "Starting execution for job with template id %s" \
                  " and execution id %s"
            self._logger.debug(msg)
            
            # read the job template object
            job_template = self.job_utils.read_job_template()

            # spawn job greenlets
            job_handler = JobHandler(self._logger, self._vnc_api,
                                     job_template, self.job_execution_id,
                                     self.job_data, self.job_params,
                                     self.job_utils, self.device_json,
                                     self.auth_token, self.tenant_name)
            result_handler = JobResultHandler(job_template,
                                              self.job_execution_id,
                                              self._logger, self.job_utils)
            if job_template.get_job_template_multi_device_job():
                self.handle_multi_device_job(job_handler, result_handler)
            else:
                self.handle_single_job(job_handler, result_handler)

            timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            result_handler.create_job_summary_log(timestamp)

        except JobException as e:
            self._logger.error("Job Exception recieved: %s" % e.msg)
            self._logger.error("%s" % traceback.print_stack())        
        except Exception as e:
            self._logger.error("Error while executing job %s " % repr(e))
            self._logger.error("%s" % traceback.print_stack())

    def handle_multi_device_job(self, job_handler, result_handler):
        job_worker_pool = Pool(POOL_SIZE)
        for device_id in self.job_params['device_list']:
            job_worker_pool.start(Greenlet(job_handler.handle_device_job,
                                           device_id, result_handler))
        job_worker_pool.join()

    def handle_single_job(self, job_handler, result_handler):
        job_handler.handle_job(result_handler)


def parse_args():
    parser = argparse.ArgumentParser(description='Job manager parameters')
    parser.add_argument('-i', '--job_input', nargs=1,
                        help='Job manager input json')
    return parser.parse_args()


if __name__ == "__main__":

    # TODO the logs before the sandesh is initalized should go to some log file
    # parse the params passed to the job manager process
    job_input_json = None
    try:
        job_params = parse_args()
        job_input_json = json.loads(job_params.job_input[0])
        if job_input_json is None:
            sys.exit("Job input data is not passed to job mgr. "
                     "Aborting job ...")

        # TODO integrate with Sandesh logger
        logger = JobLogger()
    except Exception as e:
        print >> sys.stderr, "Failed to initialize logger due "\
                             "to Exception: %s" % traceback.print_stack()
        sys.exit(
            "Exiting due to logger initialization error: %s" % repr(e))

    # initialize _vnc_api instance
    vnc_api = None
    try:
        auth_token = job_input_json['auth_token']
        tenant_name = job_input_json['tenant_name']
        vnc_api = VncApi(auth_token=auth_token, tenant_name=tenant_name)
        logger.info("VNC api is initialized using the auth token"\
                    " and tenant_name passed.")
    except Exception as e:
        logger.error("Caught exception when initialing vnc api: "
                     "%s" % traceback.print_stack())
        sys.exit("Exiting due to vnc api initialization error: %s" % repr(e))

    # invoke job manager
    try:
        job_manager = JobManager(logger, vnc_api, job_input_json)
        job_manager.start_job()
    except Exception as e:
        logger.error("Caught exception when running the job: "
                     "%s" % traceback.print_stack())
        sys.exit("Existing job due to error: %s " % repr(e))

