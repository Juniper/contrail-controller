#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains job manager api which involves playbook interactions
"""

import os
import random
import uuid
import json
import subprocess32
import traceback
import ast
import time
import gevent
import requests
import xml.etree.ElementTree as etree

from job_manager.job_exception import JobException
from job_manager.job_utils import (
    JobStatus, JobFileWrite
)
from job_manager.job_messages import MsgBundle


class JobHandler(object):

    def __init__(self, logger, vnc_api, job_template, execution_id, input,
                 job_utils, device_json, auth_token, api_server_host,
                 job_log_utils, sandesh_args, \
                 fabric_fq_name,
                 playbook_timeout, playbook_seq):
        self._logger = logger
        self._vnc_api = vnc_api
        self._job_template = job_template
        self._execution_id = execution_id
        self._job_input = input
        self._job_utils = job_utils
        self._device_json = device_json
        self._auth_token = auth_token
        self._api_server_host = api_server_host
        self._job_log_utils = job_log_utils
        self._sandesh_args = sandesh_args
        self._fabric_fq_name = fabric_fq_name
        self._playbook_timeout = playbook_timeout
        self._playbook_seq = playbook_seq
    # end __init__

    def handle_job(self, result_handler, job_percent_per_task,
                   device_id=None, device_name=None):
        playbook_output = None
        try:
            msg = "Starting playbook execution for job template %s with " \
                  "execution id %s" % (self._job_template.get_uuid(),
                                       self._execution_id)
            self._logger.debug(msg)

            # get the playbook information from the job template
            playbook_info = self.get_playbook_info(job_percent_per_task,
                                                   device_id)
            # run the playbook and retrieve the playbook output if any
            playbook_output = self.run_playbook(
                playbook_info,
                result_handler.percentage_completed)

            msg = MsgBundle.getMessage(
                MsgBundle.PLAYBOOK_EXECUTION_COMPLETE,
                job_template_name=self._job_template.get_fq_name()[-1],
                job_execution_id=self._execution_id)
            self._logger.debug(msg)
            result_handler.update_job_status(JobStatus.SUCCESS, msg,
                                             device_id, device_name)
            if playbook_output:
                result_handler.update_playbook_output(playbook_output)

            if self.current_percentage:
                result_handler.percentage_completed = self.current_percentage

        except JobException as job_exp:
            self._logger.error("%s" % job_exp.msg)
            self._logger.error("%s" % traceback.format_exc())
            result_handler.update_job_status(JobStatus.FAILURE, job_exp.msg,
                                             device_id, device_name)
        except Exception as exp:
            self._logger.error("Error while executing job %s " % repr(exp))
            self._logger.error("%s" % traceback.format_exc())
            result_handler.update_job_status(JobStatus.FAILURE, exp.message,
                                             device_id, device_name)
    # end handle_job

    def get_playbook_info(self, job_percent_per_task, device_id=None):
        try:
            # create the cmd line param for the playbook
            extra_vars = {
                'input': self._job_input,
                'job_template_id': self._job_template.get_uuid(),
                'job_template_fqname': self._job_template.fq_name,
                'fabric_fq_name': self._fabric_fq_name,
                'auth_token': self._auth_token,
                'api_server_host': self._api_server_host,
                'job_execution_id': self._execution_id,
                'args': self._sandesh_args,
                'playbook_job_percentage': job_percent_per_task
            }
            playbooks = self._job_template.get_job_template_playbooks()

            if device_id:
                if not self._device_json:
                    msg = MsgBundle.getMessage(MsgBundle.DEVICE_JSON_NOT_FOUND)
                    raise JobException(msg,
                                       self._execution_id)

                device_data = self._device_json.get(device_id)
                if not device_data:
                    msg = MsgBundle.getMessage(MsgBundle.NO_DEVICE_DATA_FOUND,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                device_family = device_data.get('device_family')
                device_vendor = device_data.get('device_vendor')
                device_product = device_data.get('device_product')

                if not device_vendor or not device_family:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               DEVICE_VENDOR_FAMILY_MISSING,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                if not device_product:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               PRODUCT_NAME_MISSING,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                # check for credentials,required param; else playbooks
                # will fail
                device_username = device_data.get('device_username')
                device_password = device_data.get('device_password')

                if not device_username or not device_password:
                    msg = MsgBundle.getMessage(MsgBundle.
                                               NO_CREDENTIALS_FOUND,
                                               device_id=device_id)
                    raise JobException(msg, self._execution_id)

                # update extra-vars to reflect device-related params
                device_fqname = device_data.get('device_fqname')
                device_management_ip = device_data.get('device_management_ip')
                extra_vars.update({
                    'device_id': device_id,
                    'device_fqname': device_fqname,
                    'device_management_ip':
                        device_management_ip,
                    'vendor': device_vendor,
                    'device_family': device_family,
                    'device_username': device_username,
                    'device_password': device_password,
                    'product_name': device_product
                })

                self._logger.debug("Passing the following device "
                                   "ip to playbook %s " % device_management_ip)

            # get the playbook uri from the job template
            play_info = playbooks.playbook_info[self._playbook_seq]

            playbook_input = {'playbook_input': extra_vars}

            playbook_info = dict()
            playbook_info['uri'] = play_info.playbook_uri
            playbook_info['extra_vars'] = playbook_input

            return playbook_info
        except JobException:
            raise
        except Exception as exp:
            msg = MsgBundle.getMessage(
                      MsgBundle.GET_PLAYBOOK_INFO_ERROR,
                      job_template_id=self._job_template.get_uuid(),
                      exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)
    # end get_playbook_info

    def process_file_and_get_marked_output(self, unique_pb_id,
                                           exec_id, playbook_process, 
                                           pr_uve_name):
        f_read = None
        marked_output = {}
        self.prouter_info = []
        total_output = ''
        markers = [
            JobFileWrite.PLAYBOOK_OUTPUT,
            JobFileWrite.JOB_PROGRESS,
            JobFileWrite.JOB_MANAGER,
            JobFileWrite.JOB_LOG,
            JobFileWrite.PROUTER_LOG
        ]
        # read file as long as it is not a time out
        # Adding a residue timeout of 5 mins in case
        # the playbook dumps all the output towards the
        # end and it takes sometime to process and read it
        file_read_timeout = self._playbook_timeout + 300
        # initialize it to the time when the subprocess
        # was spawned
        last_read_time = time.time()

        while True:
            try:
                f_read = open("/tmp/"+exec_id, "r")
                self._logger.info("File got created for exec_id %s.. "
                                  "proceeding to read contents.." % exec_id)
                current_time = time.time()
                while current_time - last_read_time < file_read_timeout:
                    line_read = f_read.readline()
                    if line_read:
                        self._logger.info("Line read ..." + line_read)
                        if unique_pb_id in line_read:
                            total_output =\
                                line_read.split(unique_pb_id)[-1].strip()
                            if total_output == 'END':
                                break
                            if JobFileWrite.JOB_MANAGER in line_read or JobFileWrite.JOB_LOG in line_read \
                                    or JobFileWrite.PROUTER_LOG in line_read:
                                total_output = line_read.strip()

                        if total_output is not None:
                            for marker in markers:
                                if marker in total_output:
                                    marked_output[marker] = total_output
                                    marked_jsons = self._extract_marked_json(
                                        marked_output
                                    )
                                    if marker == JobFileWrite.JOB_PROGRESS:
                                        self._job_progress =\
                                            marked_jsons.get(JobFileWrite.JOB_PROGRESS)
                                        self.current_percentage += float(
                                            self._job_progress)
                                        self._job_log_utils.\
                                            send_job_execution_uve(
                                                self._fabric_fq_name,
                                                self._job_template.fq_name,
                                                exec_id,
                                                percentage_completed=
                                                self.current_percentage)
                                        if pr_uve_name:
                                            self.send_prouter_job_uve(
                                                self._job_template.fq_name,
                                                pr_uve_name,
                                                exec_id,
                                                "IN_PROGRESS",
                                                self.current_percentage
                                            )
                                    elif marker == JobFileWrite.JOB_MANAGER:
                                        output = marked_jsons.get(JobFileWrite.JOB_MANAGER)
                                        if output.has_key('prouter_info'):
                                            self.prouter_info.append(
                                                output.get('prouter_info'))
                                    elif marker == JobFileWrite.JOB_LOG:
                                        self.send_job_log(
                                            **marked_jsons.get(marker)
                                        )
                                    elif marker == JobFileWrite.PROUTER_LOG:
                                        self.send_prouter_log(
                                            **marked_jsons.get(marker)
                                        )
                    else:
                        # this sleep is essential
                        # to yield the context to
                        # sandesh uve for % update
                        gevent.sleep(0)

                    current_time = time.time()
                break
            except IOError as file_not_found_err:
                self._logger.debug("File not yet created for exec_id %s !!" % exec_id)
                # check if the sub-process died but file is not created
                # if yes, it is old-usecase or there were no markers
                if playbook_process.poll() is not None:
                    self._logger.info("No markers found....")
                    break
                # for the case when the sub-process hangs for some
                # reason and does not write to/create file
                elif time.time() - last_read_time > file_read_timeout:
                    self._logger.info("Sub process probably hung; "
                                      "stopping file processing ....")
                    break
                time.sleep(0.5)
            finally:
                if f_read is not None:
                    f_read.close()
                    self._logger.info("File closed successfully!")

        return marked_output
    # end process_file_and_get_marked_output

    def send_prouter_job_uve(self, job_template_fqname, pr_uve_name, exec_id,
                         job_status, percentage_completed):
        try:
            self._logger.info("Calling send_prouter_job_uve(%s, %s, %s, %s, %s)" % (
                job_template_fqname,
                pr_uve_name,
                str(exec_id),
                job_status,
                percentage_completed
            ))

            self._job_log_utils.send_prouter_job_uve(
                job_template_fqname,
                pr_uve_name,
                exec_id,
                job_status,
                percentage_completed
            )
            self._logger.info("DONE")

        except JobException as ex:
            self._logger.error("Failed to send prouter uve: %s" % str(ex))

    def send_job_log(self, **job_log):
        try:
            self._logger.info("Send_job_log(%s)" % json.dumps(job_log))
            self._job_log_utils.send_job_log(**job_log)
            self._logger.info("DONE")
        except JobException as ex:
            self._logger.error("Failed to send job log: %s" % str(ex))

    def send_prouter_log(self, **prouter_log):
        try:
            self._logger.info("Send_prouter_log(%s)" % json.dumps(prouter_log))
            self._job_log_utils.send_prouter_object_log(**prouter_log)
            self._logger.info("DONE")
        except JobException as ex:
            self._logger.error("Failed to send prouter log: %s" % str(ex))

    def send_prouter_uve(self, exec_id, pb_status):
        status = "SUCCESS"

        if pb_status != 0:
            status = "FAILURE"

        job_template_fq_name = ':'.join(
            map(str, self._job_template.fq_name))
        for each_prouter in self.prouter_info:
            pr_fabric_job_template_fq_name = each_prouter.get('prouter_name') + \
                                             ":" + self._fabric_fq_name + ":" + \
                                             job_template_fq_name
            try:
                self._job_log_utils.send_prouter_job_uve(
                    self._job_template.fq_name,
                    pr_fabric_job_template_fq_name,
                    exec_id,
                    prouter_state=each_prouter.get('prouter_state'),
                    job_status=status
                )
            except JobException as ex:
                self._logger.error("Failed to send prouter log: %s" % str(ex))

    # end send_pr_object_log

    def run_playbook_process(self, playbook_info, percentage_completed):
        playbook_process = None
        playbook_output = None
        pr_uve_name = None

        self.current_percentage = percentage_completed
        try:
            playbook_exec_path = os.path.dirname(__file__) \
                + "/playbook_helper.py"
            unique_pb_id = str(uuid.uuid4())
            playbook_info['extra_vars']['playbook_input']['unique_pb_id']\
                = unique_pb_id
            exec_id =\
                playbook_info['extra_vars']['playbook_input'][
                    'job_execution_id']

            device_fqname = \
                playbook_info['extra_vars']['playbook_input'].get(
                    'device_fqname')
            if device_fqname:
                pr_fqname = ':'.join(map(str, device_fqname))
                job_template_fq_name = ':'.join(map(str, self._job_template.fq_name))
                pr_uve_name = pr_fqname + ":" + \
                    self._fabric_fq_name + ":" + job_template_fq_name

            pr_object_log_start_time = time.time()

            playbook_process = subprocess32.Popen(["python",
                                                   playbook_exec_path,
                                                   "-i",
                                                   json.dumps(playbook_info)],
                                                  close_fds=True, cwd='/')
            # this is to yield the context to the playbooks so that
            # they start running concurrently
            gevent.sleep(0)
            marked_output = self.process_file_and_get_marked_output(
                unique_pb_id, exec_id, playbook_process, pr_uve_name)

            marked_jsons = self._extract_marked_json(marked_output)
            playbook_output = marked_jsons.get(JobFileWrite.PLAYBOOK_OUTPUT)
            playbook_process.wait(timeout=self._playbook_timeout)
            pr_object_log_end_time = time.time()

            # create prouter UVE in job_manager only if it is not a multi
            # device job template
            if not self._job_template.get_job_template_multi_device_job():
                self.send_prouter_uve(exec_id, playbook_process.returncode)

        except subprocess32.TimeoutExpired as timeout_exp:
            if playbook_process is not None:
                os.kill(playbook_process.pid, 9)
            msg = MsgBundle.getMessage(
                      MsgBundle.RUN_PLAYBOOK_PROCESS_TIMEOUT,
                      playbook_uri=playbook_info['uri'],
                      exc_msg=repr(timeout_exp))
            raise JobException(msg, self._execution_id)

        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.
                                       RUN_PLAYBOOK_PROCESS_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)

        if playbook_process.returncode != 0:
            msg = MsgBundle.getMessage(MsgBundle.
                                       PLAYBOOK_EXIT_WITH_ERROR,
                                       playbook_uri=playbook_info['uri'])

            if playbook_output:
                msg = msg + "\n Error Message from playbook: %s" % playbook_output.get('message', "")
            raise JobException(msg, self._execution_id)

        return playbook_output

    # end run_playbook_process

    def run_playbook(self, playbook_info, percentage_completed):
        playbook_output = None
        try:
            # create job log to capture the start of the playbook
            device_name = \
                playbook_info['extra_vars']['playbook_input'].get(
                    'device_fqname')
            if device_name:
                device_name = device_name[-1]
            playbook_name = playbook_info['uri'].split('/')[-1]

            msg = MsgBundle.getMessage(MsgBundle.START_EXE_PB_MSG,
                                       playbook_name=playbook_name)
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             self._fabric_fq_name,
                                             msg, JobStatus.IN_PROGRESS.value,
                                             device_name=device_name)

            if not os.path.exists(playbook_info['uri']):
                msg = MsgBundle.getMessage(MsgBundle.PLAYBOOK_NOT_FOUND,
                                           playbook_uri=playbook_info['uri'])
                raise JobException(msg,
                                   self._execution_id)

            # Run playbook in a separate process. This is needed since
            # ansible cannot be used in a greenlet based patched environment
            playbook_output = self.run_playbook_process(playbook_info, percentage_completed)

            # create job log to capture completion of the playbook execution
            msg = MsgBundle.getMessage(MsgBundle.STOP_EXE_PB_MSG,
                                       playbook_name=playbook_name)
            self._logger.debug(msg)
            self._job_log_utils.send_job_log(self._job_template.fq_name,
                                             self._execution_id,
                                             self._fabric_fq_name,
                                             msg, JobStatus.IN_PROGRESS.value,
                                             device_name=device_name)
            return playbook_output
        except JobException:
            raise
        except Exception as exp:
            msg = MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_ERROR,
                                       playbook_uri=playbook_info['uri'],
                                       exc_msg=repr(exp))
            raise JobException(msg, self._execution_id)
    # end run_playbook

    def _extract_marked_json(self, marked_output):
        retval = {}
        for marker, output in marked_output.iteritems():
            start = output.index(marker) + len(marker)
            end = output.rindex(marker)
            if start > end:
                self._logger.error("Invalid marked output: %s" % output)
                continue
            json_str = output[start:end]
            self._logger.info("Extracted marked output: %s" % json_str)
            try:
                if marker == JobFileWrite.JOB_MANAGER:
                    retval[marker] = ast.literal_eval(json_str)
                else:
                    retval[marker] = json.loads(json_str)
            except ValueError as e:
                # assuming failure is due to unicoding in the json string
                retval[marker] = ast.literal_eval(json_str)

        return retval
    # end _extract_marked_json
