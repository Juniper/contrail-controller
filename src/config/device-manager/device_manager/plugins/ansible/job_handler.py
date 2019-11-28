#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
# This file contains implementation of job api handler code
#

from builtins import object
from builtins import str
from enum import Enum
import time
import uuid

from attrdict import AttrDict
import gevent


class JobStatus(Enum):
    STARTING = "STARTING"
    IN_PROGRESS = "IN_PROGRESS"
    SUCCESS = "SUCCESS"
    FAILURE = "FAILURE"
    WARNING = "WARNING"

    @staticmethod
    def from_str(status):
        if status == JobStatus.STARTING.value:
            return JobStatus.STARTING
        elif status == JobStatus.IN_PROGRESS.value:
            return JobStatus.IN_PROGRESS
        elif status == JobStatus.SUCCESS.value:
            return JobStatus.SUCCESS
        elif status == JobStatus.FAILURE.value:
            return JobStatus.FAILURE
        elif status == JobStatus.WARNING.value:
            return JobStatus.WARNING
        else:
            raise NotImplementedError
# end class JobStatus


class JobHandler(object):

    JOB_REQUEST_EXCHANGE = "job_request_exchange"
    JOB_REQUEST_ROUTING_KEY = "job.request"
    JOB_STATUS_CONSUMER = "job_status_consumer."
    JOB_STATUS_EXCHANGE = "job_status_exchange"
    JOB_STATUS_ROUTING_KEY = "job.status."

    def __init__(self, job_template_name, job_input, device_list,
                 api_server_config, logger, amqp_client,
                 transaction_id, transaction_descr, args):
        """Initialize JobHandler init params."""
        self._job_template_name = job_template_name
        self._job_input = job_input
        self._device_list = device_list
        self._api_server_config = api_server_config
        self._logger = logger
        self._job_id = None
        self._job_status = None
        self._amqp_client = amqp_client
        self._transaction_id = transaction_id
        self._transaction_descr = transaction_descr
        self._args = args
        super(JobHandler, self).__init__()
    # end __init__

    def push(self, timeout, max_retries):
        try:
            self._job_status = JobStatus.STARTING
            # generate the job uuid
            job_execution_id = str(int(round(time.time() * 1000))) + '_' + str(
                uuid.uuid4())

            self._logger.debug("job handler: Starting job for (%s, %s, %s)" %
                               (self._device_list,
                                str(self._job_template_name),
                                str(job_execution_id)))

            # build the job input json
            job_payload = self._generate_job_request_payload(job_execution_id)

            # create a RabbitMQ listener to listen to the job status update
            self._start_job_status_listener(job_execution_id)

            # publish message to RabbitMQ to execute job
            self._publish_job_request(job_payload)

            self._logger.debug(
                "job handler: Published job request "
                "(%s, %s, %s)" % (self._device_list, str(
                    self._job_template_name), str(job_execution_id)))

            # wait for the job status update
            self._wait(job_execution_id, timeout, max_retries)

            # remove RabbitMQ listener
            self._stop_job_status_listener(job_execution_id)

        except Exception as e:
            self._logger.error(
                "job handler: push failed for (%s, %s)"
                " execution id %s: %s" % (
                    self._device_list, str(self._job_template_name),
                    job_execution_id, repr(e)))
            self._job_status = JobStatus.FAILURE

        if self._job_status == JobStatus.FAILURE:
            raise Exception("job handler: push failed for (%s, %s)"
                            " execution id %s" % (self._device_list,
                                                  str(self._job_template_name),
                                                  job_execution_id))

        self._logger.debug("job handler: push succeeded for (%s, %s)"
                           " execution id %s" % (self._device_list,
                                                 str(self._job_template_name),
                                                 job_execution_id))

    def _start_job_status_listener(self, job_execution_id):
        # TODO - Restarting the worker might not scale and have issue
        # in parallel use amongst the greenlets
        self._amqp_client.add_consumer(
            self.JOB_STATUS_CONSUMER + job_execution_id,
            self.JOB_STATUS_EXCHANGE,
            routing_key=self.JOB_STATUS_ROUTING_KEY + job_execution_id,
            callback=self._handle_job_status_change_notification,
            auto_delete=True)

    def _stop_job_status_listener(self, job_execution_id):
        self._amqp_client.remove_consumer(
            self.JOB_STATUS_CONSUMER + job_execution_id)

    def _publish_job_request(self, job_payload):
        try:
            self._amqp_client.publish(
                job_payload, self.JOB_REQUEST_EXCHANGE,
                routing_key=self.JOB_REQUEST_ROUTING_KEY,
                serializer='json', retry=True,
                retry_policy={'max_retries': 12,
                              'interval_start': 2,
                              'interval_step': 5,
                              'interval_max': 15})
        except Exception as e:
            msg = "Failed to send job request via RabbitMQ %s " % repr(e)
            self._logger.error(msg)
            raise

    def _generate_job_request_payload(self, job_execution_id):
        job_input_json = {
            "job_execution_id": job_execution_id,
            "input": self._job_input,
            "job_template_fq_name": self._job_template_name,
            "api_server_host": self._args.api_server_ip.split(','),
            "params": {
                "device_list": self._device_list
            },
            "vnc_api_init_params": {
                "admin_user": self._args.admin_user,
                "admin_password": self._args.admin_password,
                "admin_tenant_name":
                    self._args.admin_tenant_name,
                "api_server_port": self._args.api_server_port,
                "api_server_use_ssl":
                    self._args.api_server_use_ssl
            },
            "cluster_id": self._args.cluster_id,
            "job_transaction_id": self._transaction_id,
            "job_transaction_descr": self._transaction_descr
        }
        return job_input_json

    # listens to the job notifications and updates the in memory job status
    def _handle_job_status_change_notification(self, body, message):
        try:
            message.ack()
            payload = AttrDict(body)
            self._job_status = JobStatus.from_str(payload.job_status)
        except Exception as e:
            msg = "Exception while handling the job status update " \
                  "notification %s " % repr(e)
            self._logger.error(msg)
            raise
        pass

    def _wait(self, job_execution_id, timeout, max_retries):
        retry_count = 0
        while not self._is_job_done():
            if retry_count >= max_retries:
                self._logger.error(
                    "job handler: timed out waiting for job %s for device"
                    " %s and job_type %s:" %
                    (job_execution_id, self._device_list,
                     str(self._job_template_name)))
                self._job_status = JobStatus.FAILURE
            else:
                retry_count += 1
                gevent.sleep(timeout)
    # end _wait

    def get_job_status(self):
        return self._job_status
    # end get_job_status

    def _is_job_done(self):
        if self._job_status == JobStatus.SUCCESS or \
                self._job_status == JobStatus.FAILURE or \
                self._job_status == JobStatus.WARNING:
            return True
        return False
    # end _is_job_done

# end class JobHandler
