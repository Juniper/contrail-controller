#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Job manager logger.
"""


from sandesh_common.vns.ttypes import Module
from cfgm_common.vnc_logger import ConfigServiceLogger


class JobLogger(ConfigServiceLogger):

    def __init__(self, args=None, http_server_port=None):
        module = Module.FABRIC_ANSIBLE
        module_pkg = "job_manager"
        self.context = "job_manager"
        super(JobLogger, self).__init__(
            module, module_pkg, args, http_server_port)

    def sandesh_init(self, http_server_port=None):
        super(JobLogger, self).sandesh_init(http_server_port)
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)

