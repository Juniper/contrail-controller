#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Job manager logger.
"""

class JobLogger():

    def __init__(self):
        self.job_log_file = open("/tmp/job_logs.txt", "w+")
        self._sandesh = None

    def close_logger(self):
        self.job_log_file.close()

    def error(self, message):
        self.job_log_file.write(message + "\n")

    def debug(self, message):
        self.job_log_file.write(message + "\n")

    def info(self, message):
        self.job_log_file.write(message + "\n")
