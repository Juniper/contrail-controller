#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Initializes Sandesh instance and provides utility sandesh functions."""
from __future__ import division

import argparse
from builtins import map
from builtins import object
from builtins import range
from six.moves.configparser import SafeConfigParser
from decimal import Decimal, getcontext
import json
import random
import time
import traceback

from cfgm_common.uve.vnc_api.ttypes import FabricJobExecution, FabricJobUve, \
    PhysicalRouterJobExecution, PhysicalRouterJobUve
from future import standard_library
standard_library.install_aliases()
from past.utils import old_div
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_base import Sandesh
from pysandesh.sandesh_base import SandeshConfig
from .sandesh.job.ttypes import JobLog
from .sandesh.job.ttypes import JobLogEntry
from .sandesh.job.ttypes import PRouterOnboardingLog
from .sandesh.job.ttypes import PRouterOnboardingLogEntry


from job_manager.job_exception import JobException
from job_manager.job_messages import MsgBundle
from job_manager.job_utils import JobFileWrite
from job_manager.logger import JobLogger
from job_manager.sandesh_utils import SandeshUtils


class JobLogUtils(object):

    # max parallel tasks that can be executed within one job
    TASK_POOL_SIZE = 20

    # PLAYBOOK TIMEOUT
    PLAYBOOK_TIMEOUT_VALUE = 3600

    def __init__(self, sandesh_instance_id=None, config_args=None,
                 sandesh=True, sandesh_instance=None):
        """Initialize JobLogUtils. Initialize sandesh instance."""
        self.sandesh_instance_id = sandesh_instance_id
        self.args = None
        self.config_logger = self.initialize_sandesh_logger(config_args,
                                                            sandesh,
                                                            sandesh_instance)
        self._job_file_write = JobFileWrite(self.config_logger)

    def get_config_logger(self):
        return self.config_logger

    def initialize_sandesh_logger(self, config_args, sandesh=True,
                                  sandesh_instance=None):
        # parse the logger args
        args = self.parse_logger_args(config_args)
        args.random_collectors = args.collectors
        if args.collectors:
            args.random_collectors = random.sample(args.collectors,
                                                   len(args.collectors))
            self.args = args
        # initialize logger
        logger = JobLogger(args=args,
                           sandesh_instance_id=self.sandesh_instance_id,
                           sandesh_instance=sandesh_instance)
        if not sandesh_instance and sandesh:
            try:
                sandesh_util = SandeshUtils(logger)
                sandesh_util.wait_for_connection_establish()
            except JobException:
                msg = MsgBundle.getMessage(
                    MsgBundle.SANDESH_INITIALIZATION_TIMEOUT_ERROR)
                raise JobException(msg)
            logger.info("Sandesh is initialized."
                        " Config logger instance created.")

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
            'playbook_timeout': self.PLAYBOOK_TIMEOUT_VALUE,
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
            config = SafeConfigParser()
            config.read(config_args['fabric_ansible_conf_file'])
            if 'DEFAULTS' in config.sections():
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
        parser.add_argument("--playbook_timeout",
                            help=("Playbook execution timeout value,"
                                  " default: 60 min"))
        SandeshConfig.add_parser_arguments(parser)
        args = parser.parse_args(list())
        args.conf_file = config_args.get('fabric_ansible_conf_file')
        args.collectors = config_args.get('collectors')
        args.host_ip = config_args.get('host_ip')
        args.zk_server_ip = config_args.get('zk_server_ip')
        args.cluster_id = config_args.get('cluster_id')
        if isinstance(args.collectors, str):
            args.collectors = args.collectors.split()
        args.sandesh_config = SandeshConfig.from_parser_arguments(args)
        self.args = args

        return args

    def send_job_log(self, job_template_fqname, job_execution_id,
                     fabric_fq_name, message, status, completion_percent=None,
                     result=None, timestamp=None, device_name="",
                     details=None, description="", transaction_id="",
                     transaction_descr=""):
        try:
            job_template_fqname = self.get_fq_name_log_str(job_template_fqname)
            if timestamp is None:
                timestamp = int(round(time.time() * 1000))
            fabric_name = fabric_fq_name.split(':')[-1] if \
                fabric_fq_name else fabric_fq_name
            details_str = json.dumps(details) if details else None
            job_log_entry = JobLogEntry(
                name=job_template_fqname, execution_id=job_execution_id,
                fabric_name=fabric_name, timestamp=timestamp,
                message=message, status=status,
                percentage_completed=completion_percent, result=result,
                device_name=device_name, details=details_str,
                description=description,
                transaction_id=transaction_id,
                transaction_descr=transaction_descr)
            job_log = JobLog(log_entry=job_log_entry)
            job_log.send(sandesh=self.config_logger._sandesh)
            self.config_logger.debug("Created job log for job template: %s, "
                                     " execution id: %s,  fabric_fq_name: %s"
                                     "status: %s, completion_percent %s, "
                                     "result: "
                                     "%s, message: %s"
                                     "tid: %s (%s), "
                                     "description: %s" % (job_template_fqname,
                                                       job_execution_id,
                                                       fabric_fq_name,
                                                       status,
                                                       completion_percent,
                                                       result, message,
                                                       transaction_id,
                                                       transaction_descr,
                                                       description))
        except Exception as e:
            msg = MsgBundle.getMessage(MsgBundle.SEND_JOB_LOG_ERROR,
                                       job_template_fqname=job_template_fqname,
                                       job_execution_id=job_execution_id,
                                       fabric_name=fabric_fq_name,
                                       exc_msg=repr(e))
            raise JobException(msg, job_execution_id)

    def send_job_execution_uve(
            self,
            fabric_fq_name,
            job_template_fqname,
            job_execution_id,
            percentage_completed=None):
        try:
            fabric_job_name = list(job_template_fqname)
            fabric_job_name.insert(0, fabric_fq_name)
            fabric_job_uve_name = ':'.join(map(str, fabric_job_name))

            job_exe_data = FabricJobExecution(
                name=fabric_job_uve_name,
                job_status='IN_PROGRESS',
                percentage_completed=percentage_completed)
            job_uve = FabricJobUve(
                data=job_exe_data,
                sandesh=self.config_logger._sandesh)
            job_uve.send(sandesh=self.config_logger._sandesh)
        except Exception as exp:
            job_template_fqname = self.get_fq_name_log_str(job_template_fqname)
            msg = MsgBundle.getMessage(MsgBundle.SEND_JOB_EXC_UVE_ERROR,
                                       job_template_fqname=job_template_fqname,
                                       job_execution_id=job_execution_id,
                                       exc_msg=repr(exp))
            raise JobException(msg, job_execution_id)

    def send_prouter_job_uve(
            self,
            job_template_fqname,
            fq_names,
            job_execution_id,
            prouter_state=None,
            job_status=None,
            percentage_completed=None,
            device_op_results="{}"):
        try:
            job_template_fqname = self.get_fq_name_log_str(job_template_fqname)
            if prouter_state is None:
                prouter_job_data = PhysicalRouterJobExecution(
                    name=fq_names,
                    execution_id=job_execution_id,
                    job_status=job_status,
                    percentage_completed=percentage_completed,
                    device_op_results=device_op_results
                )
            else:
                prouter_job_data = PhysicalRouterJobExecution(
                    name=fq_names,
                    execution_id=job_execution_id,
                    prouter_state=prouter_state,
                    job_status=job_status,
                    percentage_completed=percentage_completed,
                    device_op_results=device_op_results
                )

            prouter_job_uve = PhysicalRouterJobUve(
                data=prouter_job_data, sandesh=self.config_logger._sandesh)
            prouter_job_uve.send(sandesh=self.config_logger._sandesh)

        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.SEND_JOB_EXC_UVE_ERROR,
                                       job_template_fqname=job_template_fqname,
                                       job_execution_id=job_execution_id,
                                       exc_msg=repr(exp))
            raise JobException(msg, job_execution_id)

    def send_prouter_object_log(self, prouter_fqname, job_execution_id,
                                job_input, job_template_fqname,
                                onboarding_state,
                                os_version=None, serial_num=None,
                                timestamp=None):
        try:
            job_template_fqname = self.get_fq_name_log_str(job_template_fqname)
            prouter_fqname = self.get_fq_name_log_str(prouter_fqname)

            if timestamp is None:
                timestamp = int(round(time.time() * 1000))

            # create the prouter object log
            prouter_log_entry = PRouterOnboardingLogEntry(
                name=prouter_fqname,
                job_execution_id=job_execution_id,
                os_version=os_version,
                serial_num=serial_num,
                onboarding_state=onboarding_state,
                timestamp=timestamp,
                job_template_fqname=job_template_fqname,
                job_input=job_input)

            prouter_log = PRouterOnboardingLog(log_entry=prouter_log_entry)
            prouter_log.send(sandesh=self.config_logger._sandesh)
            self.config_logger.debug(
                "Created prouter object log for router: %s, "
                " execution id: %s,  job_template: %s, os_version: "
                "%s, serial_num: %s, onboarding_state %s" %
                (prouter_fqname,
                 job_execution_id,
                 job_template_fqname,
                 os_version,
                 serial_num,
                 onboarding_state))
        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.SEND_PROUTER_OBJECT_LOG_ERROR,
                                       prouter_fqname=prouter_fqname,
                                       job_execution_id=job_execution_id,
                                       exc_msg=repr(exp))

            raise JobException(msg, job_execution_id)

    def get_fq_name_log_str(self, fq_name):
        fq_name_log_str = ':'.join(map(str, fq_name))
        return fq_name_log_str

    # The api splits the total percentage amongst the tasks and returns the
    # job percentage value per task for both success and failure cases
    # num_tasks - Total number of tasks the job is being split into
    # buffer_task_percent - Set to true if we need to keep aside a buffer
    # value while calculating the per task percentage (to avoid overflows)
    # total_percent - Total percentage alloted to the job, that needs to be
    # split amongst the tasks
    # task_weightage_array - The weightage array which gives the weightage of
    # the tasks for calculating the per task percentage
    # task_seq_number - If the weightage array is provided, then this should
    # indicate the sequence number of the task within the array.
    # Note: The task_seq_number is initialized at 1.
    def calculate_job_percentage(self, num_tasks, buffer_task_percent=False,
                                 task_seq_number=None, total_percent=100,
                                 task_weightage_array=None):

        if num_tasks is None:
            raise JobException("Number of tasks is required to calculate "
                               "the job percentage")

        try:
            getcontext().prec = 3
            # Use buffered approach to mitigate the cumulative task percentages
            # exceeding the total job percentage
            if buffer_task_percent:
                buffer_percent = 0.05 * total_percent
                total_percent -= buffer_percent

            # if task weightage is not provided, the task will recive an
            # equally divided chunk from the total_percent based on the total
            # number of tasks
            if task_weightage_array is None:
                success_task_percent = float(old_div(Decimal(total_percent),
                                                     Decimal(num_tasks)))
            else:
                if task_seq_number is None:
                    raise JobException("Unable to calculate the task "
                                       "percentage since the task sequence "
                                       "number is not provided")
                success_task_percent = float(old_div(Decimal(
                    task_weightage_array[task_seq_number - 1] *
                    total_percent), 100))

            # based on the task sequence number calculate the percentage to be
            # marked in cases of error. This is required to mark the job to
            # 100% completion in cases of errors when successor tasks will not
            # be executed.
            failed_task_percent = None
            if task_seq_number:
                if task_weightage_array is None:
                    failed_task_percent = (num_tasks - task_seq_number + 1) *\
                        success_task_percent
                else:
                    failed_task_percent = 0.00
                    for task_index in range(task_seq_number, num_tasks):
                        task_percent = float(old_div(Decimal(
                            task_weightage_array[task_index - 1] *
                            total_percent), 100))
                        failed_task_percent += task_percent
            self.config_logger.info("success_task_percent %s "
                                    "failed_task_percent %s " %
                                    (success_task_percent,
                                     failed_task_percent))

            return success_task_percent, failed_task_percent
        except Exception as e:
            msg = "Exception while calculating the job pecentage %s " % repr(e)
            self.config_logger.error(msg)
            self.config_logger.error("%s" % traceback.format_exc())
            raise JobException(e)
