#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import ast
from builtins import map
from builtins import object
from builtins import str
import json
import os
import signal
import socket
import subprocess
import time
import traceback

from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import ResourceExistsError
from cfgm_common.uve.vnc_api.ttypes import FabricJobExecution, FabricJobUve, \
    PhysicalRouterJobExecution, PhysicalRouterJobUve
import gevent
from job_manager.job_exception import JobException
from job_manager.job_log_utils import JobLogUtils
from job_manager.job_utils import JobStatus, JobVncApi


class DeviceJobManager(object):

    JOB_REQUEST_EXCHANGE = "job_request_exchange"
    JOB_REQUEST_CONSUMER = "job_request_consumer"
    JOB_REQUEST_ROUTING_KEY = "job.request"
    JOB_ABORT_CONSUMER = "job_abort_consumer"
    JOB_ABORT_ROUTING_KEY = "job.abort"
    JOB_STATUS_EXCHANGE = "job_status_exchange"
    JOB_STATUS_CONSUMER = "job_status_consumer."
    JOB_STATUS_ROUTING_KEY = "job.status."
    JOB_STATUS_TTL = 5 * 60
    FABRIC_ZK_LOCK = "fabric-job-monitor"

    _instance = None

    def __init__(self, amqp_client, zookeeper_client, db_conn, args,
                 dm_logger):
        """Initialize ZooKeeper, RabbitMQ, Sandesh, DB conn etc."""
        DeviceJobManager._instance = self
        self._amqp_client = amqp_client
        self._zookeeper_client = zookeeper_client
        self._db_conn = db_conn
        self._args = args
        self._job_mgr_statistics = {
            'max_job_count': self._args.max_job_count,
            'running_job_count': 0
        }
        # dict of exec_id:job_status (key/value pairs)
        self.job_status = {}
        # map of running job instances. Key is the pid and value is job
        # instance info
        self._job_mgr_running_instances = {}

        job_args = {
            'collectors': self._args.collectors,
            'fabric_ansible_conf_file': self._args.fabric_ansible_conf_file,
            'host_ip': self._args.host_ip,
            'zk_server_ip': self._args.zk_server_ip,
            'cluster_id': self._args.cluster_id
        }
        self._job_args = json.dumps(job_args)

        # initialize the job logger
        self._job_log_utils = JobLogUtils(
            sandesh_instance_id="DeviceJobManager" + str(time.time()),
            config_args=self._job_args,
            sandesh_instance=dm_logger._sandesh)
        self._logger = self._job_log_utils.config_logger
        self._sandesh = self._logger._sandesh

        self._amqp_client.add_exchange(self.JOB_STATUS_EXCHANGE, type='direct')
        # add dummy consumer to initialize the exchange
        self._amqp_client.add_consumer(
            self.JOB_STATUS_CONSUMER + "dummy",
            self.JOB_STATUS_EXCHANGE,
            routing_key=self.JOB_STATUS_ROUTING_KEY + "dummy",
            auto_delete=True)

        self._amqp_client.add_exchange(self.JOB_REQUEST_EXCHANGE,
                                       type='direct')
        self._amqp_client.add_consumer(
            self.JOB_REQUEST_CONSUMER,
            self.JOB_REQUEST_EXCHANGE,
            routing_key=self.JOB_REQUEST_ROUTING_KEY,
            callback=self.handle_execute_job_request)

        abort_q_name = '.'.join([self.JOB_ABORT_CONSUMER,
                                 socket.getfqdn(self._args.host_ip)])
        self._amqp_client.add_consumer(
            abort_q_name,
            self.JOB_REQUEST_EXCHANGE,
            routing_key=self.JOB_ABORT_ROUTING_KEY,
            callback=self.handle_abort_job_request)
    # end __init__

    @classmethod
    def get_instance(cls):
        return cls._instance
    # end get_instance

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if not inst:
            return
        cls._instance = None
    # end destroy_instance

    def db_read(self, obj_type, obj_id, obj_fields=None,
                ret_readonly=False):
        try:
            (ok, cassandra_result) = self._db_conn.object_read(
                obj_type, [obj_id], obj_fields, ret_readonly=ret_readonly)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), let caller decide if this can be handled gracefully
            # by re-raising
            if e._unknown_id == obj_id:
                raise

            return (False, str(e))

        return (ok, cassandra_result[0])
    # end db_read

    def is_max_job_threshold_reached(self):
        if self._job_mgr_statistics.get('running_job_count') < \
                self._job_mgr_statistics.get('max_job_count'):
            return False
        return True
    # end is_max_job_threshold_reached

    def publish_job_status_notification(self, job_execution_id, status):
        try:
            msg = {'job_execution_id': job_execution_id,
                   'job_status': status}
            self._amqp_client.publish(
                msg, self.JOB_STATUS_EXCHANGE,
                routing_key=self.JOB_STATUS_ROUTING_KEY + job_execution_id,
                serializer='json', retry=True,
                retry_policy={'max_retries': 5,
                              'interval_start': 2,
                              'interval_step': 3,
                              'interval_max': 15},
                expiration=self.JOB_STATUS_TTL)
        except Exception:
            self._logger.error("Failed to send job status change notification"
                               " %s %s" % (job_execution_id, status))
    # end publish_job_status_notification

    def get_job_template_id(self, job_template_fq_name):
        try:
            return self._db_conn.fq_name_to_uuid("job_template",
                                                 job_template_fq_name)
        except Exception as e:
            msg = "Error while reading job_template_id: " + str(e)
            self._logger.error(msg)
            raise
    # end get_job_template_id

    def handle_execute_job_request(self, body, message):
        job_input_params = body
        job_execution_id = job_input_params.get('job_execution_id')
        # check if the max job processing threshold is reached
        if not self.is_max_job_threshold_reached():
            message.ack()
            self._logger.info("SENT JOB REQUEST: {}".format(
                job_execution_id))
        else:
            # requeue the message if the max threshold is reached, to be picked
            # by another job manager or wait until the current job mgr is free
            message.reject(requeue=True)
            self._logger.info("REQUEUE JOB REQUEST: {}".format(
                job_execution_id))
            gevent.sleep(1)
            return

        acfg = job_input_params.get('input', {}).get('device_abstract_config')
        if acfg:
            job_input_params['input']['device_abstract_config'] = \
                json.loads(acfg)
        update_uve_on_failure = False
        device_list = None
        extra_params = job_input_params.get('params')
        if extra_params is not None:
            device_list = extra_params.get('device_list')

        is_delete = job_input_params.get('input').get('is_delete')
        job_template_fq_name = job_input_params.get('job_template_fq_name')
        job_template_id = job_input_params.get('job_template_id')

        fabric_fq_name = None
        fabric_job_uve_name = ''
        job_input_params['vnc_api_init_params'] = {
            "admin_user": self._args.admin_user,
            "admin_password": self._args.admin_password,
            "admin_tenant_name": self._args.admin_tenant_name,
            "api_server_port": self._args.api_server_port,
            "api_server_use_ssl": self._args.api_server_use_ssl
        }

        try:

            # populate job template id if not present in input_param
            if job_template_id is None:
                job_template_id = self.get_job_template_id(
                    job_template_fq_name)
                job_input_params["job_template_id"] = job_template_id

            # read the device object and pass the necessary data to the job
            if device_list:
                self.read_device_data(device_list, job_input_params,
                                      job_execution_id, is_delete)
            else:
                self.read_fabric_data(job_input_params, job_execution_id,
                                      is_delete)

            # read the job concurrency level from job template
            job_concurrency = self.get_job_concurrency(job_template_id,
                                                       job_execution_id)
            job_input_params['job_concurrency'] = job_concurrency

            fabric_fq_name = job_input_params.get('fabric_fq_name')
            fabric_job_uve_name_list = job_template_fq_name
            fabric_job_uve_name_list.insert(0, fabric_fq_name)
            fabric_job_uve_name = ':'.join(map(str, fabric_job_uve_name_list))

            device_fqnames = []

            # create the UVE
            if fabric_fq_name != "__DEFAULT__" and not device_list:
                self.create_fabric_job_uve(fabric_job_uve_name,
                                           job_input_params.get(
                                               'job_execution_id'),
                                           JobStatus.STARTING.value, 0.0)
            if device_list:
                device_fqnames = self.create_physical_router_job_uve(
                    device_list, job_input_params, fabric_job_uve_name,
                    JobStatus.STARTING.value, 0.0)

            # after creating the UVE, flag indicates to update the
            # UVE upon any failures
            update_uve_on_failure = True

            # check if there is any other job running for the fabric
            if job_concurrency is not None and job_concurrency == "fabric":
                existing_job = self._is_existing_job_for_fabric(
                    fabric_fq_name, job_execution_id)
                if existing_job:
                    msg = "Another job for the same fabric is in" \
                          " progress. Please wait for the job to finish"
                    self.mark_failure(
                        msg, job_template_fq_name,
                        job_execution_id,
                        fabric_fq_name, mark_uve=True,
                        device_list=device_list,
                        fabric_job_uve_name=fabric_job_uve_name,
                        job_params=job_input_params)
                    return

            start_time = time.time()
            signal_var = {
                'fabric_name': fabric_job_uve_name,
                'fabric_fq_name': fabric_fq_name,
                'start_time': start_time,
                'exec_id': job_execution_id,
                'device_fqnames': device_fqnames,
                'job_concurrency': job_concurrency
            }

            self.job_status.update(
                {job_execution_id: JobStatus.STARTING.value})
            self.publish_job_status_notification(job_execution_id,
                                                 JobStatus.STARTING.value)

            # handle process exit signal
            signal.signal(signal.SIGCHLD, self.job_mgr_signal_handler)

            # write the abstract config to file if needed
            self.save_abstract_config(job_input_params)

            # add params needed for sandesh connection
            job_input_params['args'] = self._job_args

            # create job manager subprocess
            job_mgr_path = os.path.dirname(
                __file__) + "/../job_manager/job_mgr.py"
            job_process = subprocess.Popen(["python", job_mgr_path, "-i",
                                            json.dumps(job_input_params)],
                                           cwd="/", close_fds=True)

            self._job_mgr_running_instances[str(job_process.pid)] = signal_var

            self._job_mgr_statistics['running_job_count'] = len(
                self._job_mgr_running_instances)

            self._logger.notice("Created job manager process. Execution id: "
                                "%s" % job_execution_id)

            self._logger.info(
                "Current number of job_mgr processes running %s"
                % self._job_mgr_statistics.get('running_job_count'))
        except Exception as e:
            msg = "Exception while processing the job request %s %s %s : " \
                  "%s %s" % (job_template_fq_name, job_execution_id,
                             fabric_fq_name, repr(e), traceback.format_exc())
            self.mark_failure(msg, job_template_fq_name, job_execution_id,
                              fabric_fq_name, mark_uve=update_uve_on_failure,
                              device_list=device_list,
                              fabric_job_uve_name=fabric_job_uve_name,
                              job_params=job_input_params)
    # end handle_execute_job_request

    def _abort_job(self, pid, job_instance, abort_mode):
        self._logger.info("ABORT: pid={}, job_instance={}, mode={}".
                          format(pid, job_instance, abort_mode))
        # Force abort or graceful abort
        os.kill(int(pid), signal.SIGABRT if abort_mode == "force"
                else signal.SIGUSR1)

    def handle_abort_job_request(self, body, message):
        message.ack()
        inp = body.get('input')
        job_execution_ids = inp.get('job_execution_ids')
        abort_mode = inp.get('abort_mode')
        self._logger.info("Abort job request: job_ids={}, mode={}".
                          format(job_execution_ids, abort_mode))

        # Search through running job instances to find this job
        for pid, job_instance in list(self._job_mgr_running_instances.items()):
            # Abort one job
            if job_execution_ids:
                if job_instance.get('exec_id') in job_execution_ids:
                    self._abort_job(pid, job_instance, abort_mode)
            # Abort next job
            else:
                self._abort_job(pid, job_instance, abort_mode)
    # end handle_abort_job_request

    def create_fabric_job_uve(self, fabric_job_uve_name,
                              execution_id, job_status,
                              percentage_completed):
        job_execution_data = FabricJobExecution(
            name=fabric_job_uve_name,
            execution_id=execution_id,
            job_start_ts=int(round(time.time() * 1000)),
            job_status=job_status,
            percentage_completed=percentage_completed
        )
        job_execution_uve = FabricJobUve(data=job_execution_data,
                                         sandesh=self._sandesh)
        job_execution_uve.send(sandesh=self._sandesh)
    # end create_fabric_job_uve

    def create_physical_router_job_uve(self, device_list, job_input_params,
                                       fabric_job_uve_name, job_status,
                                       percentage_completed):
        device_fqnames = []
        for device_id in device_list:
            device_fqname = job_input_params.get(
                'device_json').get(device_id).get('device_fqname')
            device_fqname = ':'.join(map(str, device_fqname))
            prouter_uve_name = device_fqname + ":" + \
                fabric_job_uve_name

            prouter_job_data = PhysicalRouterJobExecution(
                name=prouter_uve_name,
                execution_id=job_input_params.get('job_execution_id'),
                job_start_ts=int(round(time.time() * 1000)),
                job_status=job_status,
                percentage_completed=percentage_completed
            )

            prouter_job_uve = PhysicalRouterJobUve(
                data=prouter_job_data, sandesh=self._sandesh)
            prouter_job_uve.send(sandesh=self._sandesh)
            device_fqnames.append(prouter_uve_name)

        return device_fqnames
    # end create_physical_router_job_uve

    def mark_failure(self, msg, job_template_fq_name, job_execution_id,
                     fabric_fq_name, mark_uve=True, device_list=None,
                     fabric_job_uve_name=None, job_params=None):
        self._logger.error("Marked job as failed %s %s %s " %
                           (job_template_fq_name, job_execution_id, msg))
        # send job object log for failure
        self._job_log_utils.send_job_log(job_template_fq_name,
                                         job_execution_id, fabric_fq_name, msg,
                                         JobStatus.FAILURE.value)
        # update the in memory job status for the job
        self.job_status[job_execution_id] = JobStatus.FAILURE.value
        self.publish_job_status_notification(job_execution_id,
                                             JobStatus.FAILURE.value)

        # update the UVE
        if mark_uve:
            if fabric_fq_name != "__DEFAULT__" and not device_list:
                self.create_fabric_job_uve(fabric_job_uve_name,
                                           job_execution_id,
                                           JobStatus.FAILURE.value, 100.0)
            if device_list:
                self.create_physical_router_job_uve(device_list,
                                                    job_params,
                                                    fabric_job_uve_name,
                                                    JobStatus.FAILURE.value,
                                                    100.0)
    # end mark_failure

    def _load_job_log(self, marker, input_str):
        json_str = input_str.split(marker)[1]
        try:
            return json.loads(json_str)
        except ValueError:
            return ast.literal_eval(json_str)
    # end _load_job_log

    def _extracted_file_output(self, execution_id):
        status = "FAILURE"
        prouter_info = {}
        device_op_results = {}
        failed_devices_list = []
        try:
            with open("/tmp/" + execution_id, "r") as f_read:
                for line in f_read:
                    if 'PROUTER_LOG##' in line:
                        job_log = self._load_job_log('PROUTER_LOG##', line)
                        fqname = ":".join(job_log.get('prouter_fqname'))
                        prouter_info[fqname] = job_log.get('onboarding_state')
                    if line.startswith('job_summary'):
                        job_log = self._load_job_log('JOB_LOG##', line)
                        status = job_log.get('job_status')
                        failed_devices_list = job_log.get(
                            'failed_devices_list')
                    if 'GENERIC_DEVICE##' in line:
                        job_log = self._load_job_log(
                            'GENERIC_DEVICE##', line)
                        device_name = job_log.get('device_name')
                        device_op_results[device_name] = job_log.get(
                            'command_output')
        except Exception as e:
            msg = "File corresponding to execution id %s not found: %s\n%s" % (
                execution_id, str(e), traceback.format_exc())
            self._logger.error(msg)

        return status, prouter_info, device_op_results, failed_devices_list
    # end _extracted_file_output

    def job_mgr_signal_handler(self, signalnum, frame):
        pid = None
        signal_var = None
        try:
            # get the child process id that called the signal handler
            pid = os.waitpid(-1, os.WNOHANG)
            signal_var = self._job_mgr_running_instances.get(str(pid[0]))
            if not signal_var:
                self._logger.error(
                    "Job mgr process %s not found in the instance "
                    "map" % str(pid))
                return

            msg = "Entered job_mgr_signal_handler for: %s" % signal_var
            self._logger.notice(msg)
            exec_id = signal_var.get('exec_id')

            status, prouter_info, device_op_results, failed_devices_list = \
                self._extracted_file_output(exec_id)
            self.job_status[exec_id] = status
            self.publish_job_status_notification(exec_id, status)

            if signal_var.get('fabric_name') != "__DEFAULT__"\
                    and not signal_var.get('device_fqnames'):
                job_execution_data = FabricJobExecution(
                    name=signal_var.get('fabric_name'),
                    job_status=status,
                    percentage_completed=100)
                job_execution_uve = FabricJobUve(data=job_execution_data,
                                                 sandesh=self._sandesh)
                job_execution_uve.send(sandesh=self._sandesh)
            else:
                for prouter_uve_name in signal_var.get('device_fqnames'):
                    prouter_status = status
                    device_name = prouter_uve_name.split(":")[1]
                    if device_name in failed_devices_list:
                        prouter_status = "FAILURE"
                    prouter_job_data = PhysicalRouterJobExecution(
                        name=prouter_uve_name,
                        job_status=prouter_status,
                        percentage_completed=100,
                        device_op_results=json.dumps(
                            device_op_results.get(device_name, {}))
                    )
                    prouter_job_uve = PhysicalRouterJobUve(
                        data=prouter_job_data, sandesh=self._sandesh)
                    prouter_job_uve.send(sandesh=self._sandesh)

            for k, v in list(prouter_info.items()):
                prouter_uve_name = "%s:%s" % (k, signal_var.get('fabric_name'))
                prouter_job_data = PhysicalRouterJobExecution(
                    name=prouter_uve_name,
                    execution_id=exec_id,
                    job_start_ts=int(round(signal_var.get('start_time'
                                                          ) * 1000)),
                    prouter_state=v
                )
                prouter_job_uve = PhysicalRouterJobUve(
                    data=prouter_job_data, sandesh=self._sandesh)
                prouter_job_uve.send(sandesh=self._sandesh)

            self._clean_up_job_data(signal_var, str(pid[0]))

            self._logger.info("Job : %s finished. Current number of job_mgr "
                              "processes running now %s " %
                              (signal_var, self._job_mgr_statistics[
                                  'running_job_count']))

        except OSError as process_error:
            self._logger.error("Could not retrieve the child process id. "
                               "OS call returned with error %s" %
                               str(process_error))
        except Exception as unknown_exception:
            self._clean_up_job_data(signal_var, str(pid[0]))
            self._logger.error("Failed in job signal handler %s" %
                               str(unknown_exception))
    # end job_mgr_signal_handler

    def _clean_up_job_data(self, signal_var, pid):
        # remove the pid entry of the processed job_mgr process
        del self._job_mgr_running_instances[pid]

        # clean up fabric level lock
        if signal_var.get('job_concurrency') \
                is not None and signal_var.get('job_concurrency') == "fabric":
            self._release_fabric_job_lock(signal_var.get('fabric_fq_name'))
        self._cleanup_job_lock(signal_var.get('fabric_fq_name'))

        self._job_mgr_statistics['running_job_count'] = len(
            self._job_mgr_running_instances)
    # end _clean_up_job_data

    def _is_existing_job_for_fabric(self, fabric_fq_name, job_execution_id):
        is_fabric_job_running = False
        # build the zk lock path
        fabric_node_path = '/job-manager/' + fabric_fq_name + '/' + \
                           self.FABRIC_ZK_LOCK
        # check if the lock is already taken if not taken, acquire the lock
        # by creating a node
        try:
            self._zookeeper_client.create_node(fabric_node_path,
                                               value=job_execution_id,
                                               ephemeral=True)
            self._logger.info("Acquired fabric lock"
                              " for %s " % fabric_node_path)
        except ResourceExistsError:
            # means the lock was acquired by some other job
            value = self._zookeeper_client.read_node(fabric_node_path)
            self._logger.error("Fabric lock is already acquired by"
                               " job %s " % value)
            is_fabric_job_running = True
        return is_fabric_job_running
    # end _is_existing_job_for_fabric

    def _release_fabric_job_lock(self, fabric_fq_name):
        # build the zk lock path
        fabric_node_path = '/job-manager/' + fabric_fq_name + '/' + \
                           self.FABRIC_ZK_LOCK
        try:
            self._zookeeper_client.delete_node(fabric_node_path)
            self._logger.info("Released fabric lock"
                              " for %s " % fabric_node_path)
        except Exception as zk_error:
            self._logger.error("Exception while releasing the zookeeper lock"
                               " %s " % repr(zk_error))
    # end _release_fabric_job_lock

    def _cleanup_job_lock(self, fabric_fq_name):
        fabric_node_path = '/job-manager/' + fabric_fq_name
        try:
            if not self._zookeeper_client.get_children(fabric_node_path):
                self._zookeeper_client.delete_node(fabric_node_path)
                self._logger.info("Released fabric node"
                                  " for %s " % fabric_node_path)
        except Exception as zk_error:
            self._logger.error("Exception while releasing the fabric node for "
                               "%s: %s " % (fabric_node_path, str(zk_error)))
    # end _cleanup_job_lock

    def save_abstract_config(self, job_params):
        # Saving device abstract config to a local file as it could be large
        # config. There will be one local file per device and this file gets
        # removed when device is removed from database.
        dev_abs_cfg = job_params.get('input', {}).get('device_abstract_config')
        if dev_abs_cfg:
            dev_mgt_ip = dev_abs_cfg.get('system', {}).get('management_ip')
            if not dev_mgt_ip:
                raise ValueError('Missing management IP in abstract config')

            dev_cfg_dir = '/opt/contrail/fabric_ansible_playbooks/config/' +\
                          dev_mgt_ip
            if not os.path.exists(dev_cfg_dir):
                os.makedirs(dev_cfg_dir)
            if dev_cfg_dir:
                with open(dev_cfg_dir + '/abstract_cfg.json', 'w') as f:
                    f.write(json.dumps(dev_abs_cfg, indent=4))
                job_params.get('input').pop('device_abstract_config')
    # end save_abstract_config

    def get_job_concurrency(self, job_template_id, job_exec_id):

        (ok, result) = self.db_read("job-template", job_template_id,
                                    ['job_template_concurrency_level'])
        if not ok:
            msg = "Error while reading the job concurrency " \
                  "from the job template with id %s : %s" %\
                  (job_template_id, result)
            raise JobException(msg, job_exec_id)
        return result.get('job_template_concurrency_level')
    # end get_job_concurrency

    def read_device_data(self, device_list, request_params,
                         job_exec_id, is_delete=False):
        device_data = dict()

        for device_id in device_list:
            if not is_delete:
                try:
                    (ok, result) = self.db_read(
                        "physical-router", device_id,
                        ['physical_router_user_credentials',
                         'physical_router_management_ip', 'fq_name',
                         'physical_router_device_family',
                         'physical_router_vendor_name',
                         'physical_router_product_name',
                         'fabric_refs'])
                    if not ok:
                        msg = "Error while reading the physical router " \
                              "with id %s : %s" % (device_id, result)
                        raise JobException(msg, job_exec_id)
                except NoIdError as ex:
                    msg = "Device not found" \
                          "%s: %s" % (device_id, str(ex))
                    raise JobException(msg, job_exec_id)
                except Exception as e:
                    msg = "Exception while reading device %s %s " % \
                          (device_id, str(e))
                    raise JobException(msg, job_exec_id)

                device_fq_name = result.get('fq_name')
                device_mgmt_ip = result.get(
                    'physical_router_management_ip')
                user_cred = result.get('physical_router_user_credentials')

                device_family = result.get("physical_router_device_family")
                device_vendor_name = result.get("physical_router_vendor_name")
                device_product_name = result.get(
                    "physical_router_product_name")

                fabric_refs = result.get('fabric_refs')
                if fabric_refs:
                    fabric_fq_name = result.get('fabric_refs')[0].get('to')
                    fabric_fq_name_str = ':'.join(fabric_fq_name)
                    request_params['fabric_fq_name'] = fabric_fq_name_str
            else:
                device_mgmt_ip = request_params.get('input', {}).get(
                    'device_management_ip')
                device_abs_cfg = request_params.get('input', {}).get(
                    'device_abstract_config')

                system = device_abs_cfg.get('system', {})
                device_name = system.get('name')
                device_username = system.get('credentials', {}).get(
                    'user_name')
                device_password = system.get('credentials', {}).get('password')
                user_cred = {
                    "username": device_username,
                    "password": device_password
                }
                device_family = system.get('device_family')
                device_vendor_name = system.get('vendor_name')
                device_product_name = system.get('product_name')
                device_fq_name = [
                    "default-global-system-config",
                    device_name
                ]
                self.read_fabric_data(request_params, job_exec_id, is_delete)

            device_json = {"device_management_ip": device_mgmt_ip}
            device_json.update({"device_fqname": device_fq_name})

            if user_cred:
                device_json.update(
                    {"device_username": user_cred.get('username')})
                decrypt_password = JobVncApi.decrypt_password(
                    encrypted_password=user_cred.get('password'),
                    pwd_key=device_id)
                device_json.update({"device_password":
                                    decrypt_password})
            if device_family:
                device_json.update({"device_family": device_family})

            if device_vendor_name:
                device_json.update({"device_vendor": device_vendor_name})

            if device_product_name:
                device_json.update({"device_product": device_product_name})

            device_data.update({device_id: device_json})

        if len(device_data) > 0:
            request_params.update({"device_json": device_data})
    # end read_device_data

    def read_fabric_data(self, request_params, job_execution_id,
                         is_delete=False):
        if request_params.get('input') is None:
            err_msg = "Missing job input"
            raise JobException(err_msg, job_execution_id)
        fabric_fq_name = None
        if request_params.get('input').get('fabric_fq_name'):
            fabric_fq_name = request_params.get('input').get('fabric_fq_name')
        elif request_params.get('input').get('fabric_uuid'):
            # get the fabric fq_name from the db if fabric_uuid is provided
            fabric_uuid = request_params.get('input').get('fabric_uuid')
            try:
                fabric_fq_name = self._db_conn.uuid_to_fq_name(fabric_uuid)
            except NoIdError as e:
                raise JobException(str(e), job_execution_id)
        else:
            if "device_deletion_template" in request_params.get(
                    'job_template_fq_name'):
                fabric_fq_name = ["__DEFAULT__"]
            elif not is_delete:
                err_msg = "Missing fabric details in the job input"
                raise JobException(err_msg, job_execution_id)
        if fabric_fq_name:
            fabric_fq_name_str = ':'.join(map(str, fabric_fq_name))
            request_params['fabric_fq_name'] = fabric_fq_name_str
    # end read_fabric_data
