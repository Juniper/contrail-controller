#!/usr/bin/python
from __future__ import print_function

import argparse
from builtins import object
from builtins import str
import sys

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog

from job_manager.job_utils import JobFileWrite


class FilterModule(object):

    def filters(self):
        return {
            'report_percentage_completion': self.report_percentage_completion,
            'report_playbook_results': self.report_playbook_results,
        }

    def get_job_ctx_details(self, job_ctx):
        return job_ctx.get('job_execution_id'), job_ctx.get('unique_pb_id')

    def report_percentage_completion(self, job_ctx, percentage):
        logger = FilterLog.instance("WritePercentToFileFilter").logger()
        job_file_write = JobFileWrite(logger)
        exec_id, unique_pb_id = self.get_job_ctx_details(job_ctx)
        job_file_write.write_to_file(
            exec_id, unique_pb_id, JobFileWrite.JOB_PROGRESS, str(percentage)
        )
        return {
            'status': 'success',
            'write_to_file_log':
            'Successfully wrote progress to streaming file'}

    def report_playbook_results(self, job_ctx, pb_results):
        logger = FilterLog.instance("WritePbResultsToFileFilter").logger()
        job_file_write = JobFileWrite(logger)
        exec_id, unique_pb_id = self.get_job_ctx_details(job_ctx)
        job_file_write.write_to_file(
            exec_id, unique_pb_id, JobFileWrite.GEN_DEV_OP_RES, str(pb_results)
        )
        return {
            'status': 'success',
            'write_to_file_log':
            'Successfully wrote command results to streaming file'}


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
            {"sample_exec_id_filename": "sample_exec_id",
             "sample_unique_pb_id": "sample_unique_pb_id"},
            10)
    print(results)
