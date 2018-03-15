#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Initializes Sandesh instance and provides utility functions to create UVE
objects and object logs. Also provides the config logger instance for
generating the config logs
"""

import time
import argparse
import random
import json
import ConfigParser

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.job.ttypes import *

from logger import JobLogger
from job_exception import JobException
from sandesh_utils import SandeshUtils


class JobLogUtils(object):

    # max parallel tasks that can be executed within one job
    TASK_POOL_SIZE = 20

    def __init__(self, sandesh_instance_id=None, args=None):
        self.sandesh_instance_id = sandesh_instance_id
        self.args = None
        self.config_logger = self.initialize_sandesh_logger(args)

    def get_config_logger(self):
        return self.config_logger

    def initialize_sandesh_logger(self, config_args):
        # parse the logger args
        args = self.parse_logger_args(config_args)
        args.random_collectors = args.collectors
        if args.collectors:
            args.random_collectors = random.sample(args.collectors,
                                                   len(args.collectors))
            self.args = args
        # initialize logger
        logger = JobLogger(args=args,
                           sandesh_instance_id=self.sandesh_instance_id)
        try:
            sandesh_util = SandeshUtils()
            sandesh_util.wait_for_connection_establish(logger)
        except JobException:
            raise JobException("Sandesh initialization timeout after 15s")
        logger.info("Sandesh is initialized. Config logger instance created.")
        return logger

    def parse_logger_args(self, config_args):
        config_args = json.loads(config_args)
        parser = argparse.ArgumentParser()

        defaults = {
            'collectors': None,
            'http_server_port': '-1',
            'log_local': False,
            'log_level': SandeshLevel.SYS_DEBUG,
            'log_category': '',
            'log_file': Sandesh._DEFAULT_LOG_FILE,
            'use_syslog': False,
            'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
            'cluster_id': '',
            'logging_conf': '',
            'logger_class': None,
            'max_job_task': self.TASK_POOL_SIZE,
        }

        defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
        secopts = {
            'use_certs': False,
            'keyfile': '',
            'certfile': '',
            'ca_certs': '',
        }
        ksopts = {}
        sandeshopts = SandeshConfig.get_default_options()

        if config_args.get("fabric_ansible_conf_file"):
            config = ConfigParser.SafeConfigParser()
            config.read(config_args['fabric_ansible_conf_file'])
            defaults.update(dict(config.items("DEFAULTS")))
            if ('SECURITY' in config.sections() and
                    'use_certs' in config.options('SECURITY')):
                if config.getboolean('SECURITY', 'use_certs'):
                    secopts.update(dict(config.items("SECURITY")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))
            SandeshConfig.update_options(sandeshopts, config)

        defaults.update(secopts)
        defaults.update(ksopts)
        defaults.update(sandeshopts)
        parser.set_defaults(**defaults)

        parser.add_argument("--collectors",
                            help="List of VNC collectors in ip:port format",
                            nargs="+")
        parser.add_argument("--http_server_port",
                            help="Port of local HTTP server")
        parser.add_argument("--log_local", action="store_true",
                            help="Enable local logging of sandesh messages")
        parser.add_argument("--log_level",
                            help="Severity level for local logging"
                                 " of sandesh messages")
        parser.add_argument("--log_category",
                            help="Category filter for local logging "
                                 "of sandesh messages")
        parser.add_argument("--log_file",
                            help="Filename for the logs to be written to")
        parser.add_argument("--use_syslog", action="store_true",
                            help="Use syslog for logging")
        parser.add_argument("--syslog_facility",
                            help="Syslog facility to receive log lines")
        parser.add_argument("--admin_user",
                            help="Name of keystone admin user")
        parser.add_argument("--admin_password",
                            help="Password of keystone admin user")
        parser.add_argument("--admin_tenant_name",
                            help="Tenant name for keystone admin user")
        parser.add_argument("--cluster_id",
                            help="Used for database keyspace separation")
        parser.add_argument("--logging_conf",
                            help=("Optional logging configuration "
                                  "file, default: None"))
        parser.add_argument("--logger_class",
                            help=("Optional external logger class,"
                                  " default: None"))
        parser.add_argument("--max_job_task",
                            help=("Maximum job tasks that can execute in "
                                  "parallel in a parent job, default: %s"
                                  % self.TASK_POOL_SIZE))
        SandeshConfig.add_parser_arguments(parser)
        args = parser.parse_args(list())
        args.conf_file = config_args.get('fabric_ansible_conf_file')
        args.collectors = config_args.get('collectors')
        if isinstance(args.collectors, str):
            args.collectors = args.collectors.split()
        args.sandesh_config = SandeshConfig.from_parser_arguments(args)
        self.args = args

        return args

    def send_job_log(self, job_template_id, job_execution_id,
                     message, status, result=None, timestamp=None):
        try:
            if timestamp is None:
                timestamp = int(round(time.time()*1000))
            job_log_entry = JobLogEntry(name=job_template_id,
                                        execution_id=job_execution_id,
                                        timestamp=timestamp, message=message,
                                        status=status, result=result)
            job_log = JobLog(log_entry=job_log_entry)
            job_log.send(sandesh=self.config_logger._sandesh)
            self.config_logger.debug("Created job log for job template: %s, "
                                     " execution id: %s,  status: %s, result: "
                                     "%s, message: %s" % (job_template_id,
                                                          job_execution_id,
                                                          status, result,
                                                          message))
        except Exception as e:
            msg = "Error while creating the job log for job template " \
                  "%s and execution id %s : %s" % (job_template_id,
                                                   job_execution_id,
                                                   repr(e))
            raise JobException(msg, self._job_execution_id)

    def send_job_execution_uve(self, job_template_id, job_execution_id,
                               timestamp=None, percentage_completed=None):
        try:
            if timestamp is None:
                timestamp = int(round(time.time()*1000))
            job_exe_data = JobExecution(
                name=job_template_id,
                execution_id=job_execution_id,
                job_start_ts=timestamp,
                percentage_completed=percentage_completed)
            job_uve = UveJobExecution(data=job_exe_data)
            job_uve.send(sandesh=self.config_logger._sandesh)
        except Exception as e:
            msg = "Error while sending the job execution UVE for job " \
                  "template %s and execution id %s : %s" % \
                  (job_template_id, job_execution_id, repr(e))
            raise JobException(msg, job_execution_id)

