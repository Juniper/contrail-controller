#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Job manager exception class
"""


class JobException(Exception):

    def __init__(self, job_execution_id=None, *args, **kwargs):
        self.job_execution_id = job_execution_id

    def __str__(self):
        return "Error from job manager, execution id %s, message %s" % \
               (self.job_execution_id, self.message)

