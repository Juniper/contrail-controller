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

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.job.ttypes import *

from logger import JobLogger
from job_exception import JobException


class JobLogUtils(object):

    def __init__(self, args_str=None):
        self.config_logger = self.initialize_sandesh_logger(args_str)

    def get_config_logger(self):
        return self.config_logger

    def initialize_sandesh_logger(self, args_str):
        # parse the logger args
        args = self.parse_logger_args(args_str)
        args.random_collectors = args.collectors
        if args.collectors:
            args.random_collectors = random.sample(args.collectors,
                                                   len(args.collectors))
        # initialize logger
        logger = JobLogger(args)
        logger.info("Sandesh is initialized. Config logger instance created.")
        return logger

    def parse_logger_args(self, args_str):
        parser = argparse.ArgumentParser(description='Job manager parameters')
        parser.add_argument('-i', '--job_input', nargs=1,
                            help='Job manager input json')

        args_str = ''
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file", action='append',
                                 help="Specify config file", metavar="FILE")
        parser = argparse.ArgumentParser()
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'cassandra_server_list': '127.0.0.1:9160',
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'collectors': None,
            'http_server_port': '8111',
            'log_local': False,
            'log_level': SandeshLevel.SYS_DEBUG,
            'log_category': '',
            'log_file': Sandesh._DEFAULT_LOG_FILE,
            'use_syslog': False,
            'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
            'cluster_id': '',
            'logging_conf': '',
            'logger_class': None,
        }

        defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
        secopts = {
            'use_certs': False,
            'keyfile': '',
            'certfile': '',
            'ca_certs': '',
        }
        ksopts = {}
        cassandraopts = {}
        sandeshopts = SandeshConfig.get_default_options()

        saved_conf_file = args.conf_file
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(args.conf_file)
            defaults.update(dict(config.items("DEFAULTS")))
            if ('SECURITY' in config.sections() and
                    'use_certs' in config.options('SECURITY')):
                if config.getboolean('SECURITY', 'use_certs'):
                    secopts.update(dict(config.items("SECURITY")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))
            if 'CASSANDRA' in config.sections():
                cassandraopts.update(dict(config.items('CASSANDRA')))
            SandeshConfig.update_options(sandeshopts, config)

        defaults.update(secopts)
        defaults.update(ksopts)
        defaults.update(cassandraopts)
        defaults.update(sandeshopts)
        parser.set_defaults(**defaults)

        parser.add_argument(
            "--cassandra_server_list",
            help="List of cassandra servers in IP Address:Port format",
            nargs='+')
        parser.add_argument(
            "--reset_config", action="store_true",
            help="Warning! Destroy previous configuration and start clean")
        parser.add_argument("--api_server_ip",
                            help="IP address of API server")
        parser.add_argument("--api_server_port",
                            help="Port of API server")
        parser.add_argument("--api_server_use_ssl",
                            help="Use SSL to connect with API server")
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
        parser.add_argument("--cassandra_user",
                            help="Cassandra user name")
        parser.add_argument("--cassandra_password",
                            help="Cassandra password")
        SandeshConfig.add_parser_arguments(parser)
        args = parser.parse_args(remaining_argv)
        if isinstance(args.cassandra_server_list, str):
            args.cassandra_server_list = args.cassandra_server_list.split()
        if isinstance(args.collectors, str):
            args.collectors = args.collectors.split()
        args.sandesh_config = SandeshConfig.from_parser_arguments(args)

        args.conf_file = saved_conf_file

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

