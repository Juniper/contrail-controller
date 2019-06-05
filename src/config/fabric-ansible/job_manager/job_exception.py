#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Job manager exception class."""


class JobException(Exception):

    # Job specific exception raised in job manager implementation
    #   Examples:
    #   raise JobException(msg="sandesh init error", job_execution_id="12345")
    #   Traceback (most recent call last):
    #   ...
    #   JobException: JobException in execution (12345): sandesh init error
    #       >>> raise JobException("sandesh init error", "12345")
    #   Traceback (most recent call last):
    #   ...
    #   JobException: JobException in execution (12345): sandesh init error

    def __init__(self, msg=None, job_execution_id=None):
        """Initializes job exception. Optional msg and exec id can be set."""
        self.job_execution_id = job_execution_id
        self.msg = msg

    def __str__(self):
        """Provides the exception as a string object."""
        return "JobException in execution (%s): %s" % \
               (self.job_execution_id, self.msg)

    def __repr__(self):
        """Provides the exception representative message."""
        return self.msg

# if __name__ == "__main__":
#     import doctest
#     doctest.run_docstring_examples(__str__, globals())
