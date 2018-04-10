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

    def unint_sandesh(self, logger):
        logger._sandesh._client._connection.set_admin_state(down=True)
        logger._sandesh.uninit()

    @timeout_decorator.timeout(15, timeout_exception=JobException)
    def check_sandesh_queue(self, logger):
        while not logger._sandesh.is_send_queue_empty():
            time.sleep(1)

    # checks and waits for the sandesh client message queue to be empty and
    # then closes the sandesh connection
    def close_sandesh_connection(self, logger):
        try:
            self.check_sandesh_queue(logger)
        except JobException as e:
            msg = "Error in confirming the SANDESH message send operation." \
                  " The Job Logs might not be complete."
            logger.error(msg)
            raise e
        finally:
            self.unint_sandesh(logger)

