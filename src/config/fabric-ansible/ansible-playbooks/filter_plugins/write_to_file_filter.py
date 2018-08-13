#!/usr/bin/python
import logging
import argparse
import traceback
import sys
import json
from job_manager.job_utils import JobUtils

class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('WriteToFileFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

        return logger
    # end _init_logging

    def __init__(self):
        self._logger = FilterModule._init_logging()
        self.job_utils = JobUtils(logger=self._logger)
    # end __init__

    def filters(self):
        return {
            'job_log_percentage_completion':
                self.job_log_percentage_completion,
            'prouter_object_log': self.prouter_object_log,
        }

    def get_job_ctx_details(self):
        return self.job_ctx.get('job_execution_id'), self.job_ctx.get(
            'unique_pb_id')

    def calculate_job_percentage(self):
        job_success_percent = None
        job_error_percent = None
        try:
            total_percent = self.job_ctx.get('playbook_job_percentage')
            if total_percent:
                total_percent = float(total_percent)
            self._logger.debug(
                "Calculating the job completion percentage. "
                "total_task_count: %s, current_task_index: %s, "
                "playbook_job_percentage: %s,"
                " task_weightage_array: %s",
                self.job_ctx.get('total_task_count'),
                self.job_ctx.get('current_task_index'),
                total_percent,
                self.job_ctx.get('task_weightage_array'))
            job_success_percent, job_error_percent = \
                self.job_utils.calculate_job_percentage(
                    self.job_ctx.get('total_task_count'),
                    buffer_task_percent=False,
                    task_seq_number=self.job_ctx.get('current_task_index'),
                    total_percent=total_percent,
                    task_weightage_array=
                    self.job_ctx.get('task_weightage_array'))
        except Exception as e:
            self._logger.error("Exception while calculating the job "
                          "percentage %s", str(e))
        if job_error_percent:
            job_percentage = job_error_percent
        else:
            job_percentage = job_success_percent
        return job_percentage


    def job_log_percentage_completion(self, job_ctx, job_log_data):
        write_to_file = {}
        self.job_ctx = job_ctx
        exec_id, unique_pb_id = self.get_job_ctx_details()
        percentage = self.calculate_job_percentage()
        write_to_file_log = "\n"
        try:
            write_to_file['percentage'] = percentage
            write_to_file.update(job_log_data)
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                line_in_file = unique_pb_id + 'JOB_PROGRESS##' +\
                    json.dumps(write_to_file) + 'JOB_PROGRESS##'
                f.write(line_in_file + '\n')
                write_to_file_log += "Written line %s to the /tmp/exec-id" \
                                     " file \n" % line_in_file
                return {
                    'status': 'success',
                    'write_to_file_log': write_to_file_log
            }
        except Exception as ex:
            self._logger.info(write_to_file_log)
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'write_to_file_log': write_to_file_log
            }

    def prouter_object_log(self, job_ctx, prouter_log_data):
        write_to_file = {}
        self.job_ctx = job_ctx
        exec_id, unique_pb_id = self.get_job_ctx_details()
        write_to_file_log = "\n"
        try:
            write_to_file.update(prouter_log_data)
            write_to_file['job_input'] = json.dumps(self.job_ctx['job_input'])
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                line_in_file = unique_pb_id + 'PROUTER_LOG##' +\
                    json.dumps(write_to_file) + 'PROUTER_LOG##'
                f.write(line_in_file + '\n')
                write_to_file_log += "Written line %s to the /tmp/exec-id" \
                                     " file \n" % line_in_file
                return {
                    'status': 'success',
                    'write_to_file_log': write_to_file_log
            }
        except Exception as ex:
            self._logger.info(write_to_file_log)
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'write_to_file_log': write_to_file_log
            }

def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-jl', '--job_log',
                        action='store_true',
                        help='write job object log to file')
    return parser.parse_args()
# end _parse_args


if __name__ == '__main__':

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.job_log:
        results = fabric_filter.job_log_percentage_completion(
            "sample_exec_id_filename",
            "sample_unique_pb_id",
            "sample_job_obj_log")
    print results
