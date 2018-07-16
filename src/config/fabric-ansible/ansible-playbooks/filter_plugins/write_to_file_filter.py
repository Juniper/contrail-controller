#!/usr/bin/python
import logging
import argparse
import traceback
import sys
import json


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
    # end __init__

    def filters(self):
        return {
            'report_percentage_completion': self.report_percentage_completion,
            'report_device_info': self.report_device_info,
            'report_pb_output': self.report_pb_output,
            'report_end_of_playbook': self.report_end_of_playbook,
        }

    def get_job_ctx_details(self, job_ctx):
        return job_ctx.get('job_execution_id'),job_ctx.get('unique_pb_id')

    def report_percentage_completion(self, job_ctx, percentage):
        exec_id, unique_pb_id = self.get_job_ctx_details(job_ctx)
        write_to_file_log = "\n"
        try:
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                line_in_file = unique_pb_id + 'JOB_PROGRESS##' +\
                    str(percentage) + 'JOB_PROGRESS##'
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

    def report_pb_output(self, job_ctx, pb_output):
        exec_id, unique_pb_id = self.get_job_ctx_details(job_ctx)
        write_to_file_log = "\n"
        try:
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                line_in_file = unique_pb_id + 'PLAYBOOK_OUTPUT##' +\
                    json.dumps(pb_output) + 'PLAYBOOK_OUTPUT##'
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

    def report_end_of_playbook(self, job_ctx):
        exec_id, unique_pb_id = self.get_job_ctx_details(job_ctx)
        write_to_file_log = "\n"
        try:
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                line_in_file = unique_pb_id + 'END'
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

    def report_device_info(self, job_ctx, device_data):
        exec_id, unique_pb_id = self.get_job_ctx_details(job_ctx)
        write_to_file_log = "\n"
        try:
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                line_in_file = unique_pb_id + 'DEVICEDATA##' +\
                    json.dumps(device_data) + 'DEVICEDATA##'
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
    parser.add_argument('-pc', '--percentage_complete',
                        action='store_true',
                        help='write percentage completion to file')
    parser.add_argument('-po', '--playbook_output',
                        action='store_true',
                        help='write playbook_output to file')
    parser.add_argument('-e', '--mark_end',
                        action='store_true',
                        help='write playbook end to file')
    parser.add_argument('-dd', '--device_data',
                        action='store_true',
                        help='write device info to file')
    return parser.parse_args()
# end _parse_args


if __name__ == '__main__':

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.percentage_complete:
        results = fabric_filter.report_percentage_completion(
            "sample_exec_id_filename",
            "sample_unique_pb_id",
            10)
    elif parser.playbook_output:
        results = fabric_filter.report_pb_output(
            "sample_exec_id_filename",
            "sample_unique_pb_id",
            {"pb_op": "sample pb_op"})
    elif parser.mark_end:
        results = fabric_filter.report_end_of_playbook(
            "sample_exec_id_filename",
            "sample_unique_pb_id")
    elif parser.device_data:
        results = fabric_filter.report_device_info(
            "sample_exec_id_filename",
            "sample_unique_pb_id",
            {"dd": "sample device_info"})
    print results
