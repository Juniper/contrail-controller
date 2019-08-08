#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
"""Contains utility functions used for Sandesh initialization and logging."""

from builtins import object
import time

from job_manager.job_exception import JobException
from job_manager.job_messages import MsgBundle


class SandeshUtils(object):

    def __init__(self, logger):
        """
        Creates an instance of SandeshUtils.

        :param logger: Sandesh logger
        """
        self._logger = logger

#    @wrapt_timeout_decorator.timeout(15, timeout_exception=JobException)
    def wait_for_connection_establish(self):
        total_wait = 0
        state = self._logger._sandesh._client._connection.\
            statemachine().state()
        while state is None or state != "Established":
            time.sleep(0.2)
            total_wait += 0.2
            if total_wait > 30:
                self._logger.error("Giving up on opening sandesh connection:"
                                   " %s" % state)
                break
            state = self._logger._sandesh._client._connection.\
                statemachine().state()

    def uninit_sandesh(self):
        self._logger._sandesh._client._connection.set_admin_state(down=True)
        self._logger._sandesh.uninit()

#    @wrapt_timeout_decorator.timeout(15, timeout_exception=JobException)
    def wait_for_msg_send(self):
        total_wait = 0
        while not self._logger._sandesh.is_send_queue_empty():
            time.sleep(0.2)
            total_wait += 0.2
            if total_wait > 30:
                state = self._logger._sandesh._client._connection.\
                    statemachine().state()
                self._logger.error("Giving up on empty sandesh send queue:"
                                   " %s" % state)
                break

    # checks and waits for the sandesh client message queue to be empty and
    # then closes the sandesh connection
    def close_sandesh_connection(self):
        try:
            self.wait_for_msg_send()
        except JobException as job_exp:
            msg = MsgBundle.getMessage(MsgBundle.CLOSE_SANDESH_EXCEPTION)
            self._logger.error(msg)
            job_exp.msg = msg
            raise job_exp
        finally:
            self.uninit_sandesh()
