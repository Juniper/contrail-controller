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


def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-pc', '--percentage_complete',
                        action='store_true',
                        help='write percentage completion to file')
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
    print results
