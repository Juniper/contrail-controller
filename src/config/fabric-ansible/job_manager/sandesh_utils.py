#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains utility functions used for Sandesh initialization and logging
"""
import time
import timeout_decorator

from job_exception import JobException


class SandeshUtils(object):

    @timeout_decorator.timeout(15, timeout_exception=JobException)
    def wait_for_connection_establish(self, logger):
        state = logger._sandesh._client._connection.statemachine().state()
        while state is None or state != "Established":
            time.sleep(1)
            state = logger._sandesh._client._connection.statemachine().state()

