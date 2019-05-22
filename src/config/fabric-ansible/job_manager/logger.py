#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Job manager logger."""

from cfgm_common.vnc_greenlets import VncGreenlet
from cfgm_common.vnc_logger import ConfigServiceLogger
from pysandesh.sandesh_base import Sandesh
from sandesh_common.vns.ttypes import Module


class JobLogger(ConfigServiceLogger):

    def __init__(self, args=None, http_server_port=None,
                 sandesh_instance_id=None, sandesh_instance=None):
        """
        Initialize Job Logger.

        :param args: Config params passed to job manager
        :param http_server_port: Required for Sandesh logger initialization
        :param sandesh_instance_id: Uniquely identifies the logger instance
        :param sandesh_instance: Optional sandesh instance
        """
        self.sandesh_instance_id = sandesh_instance_id
        self._sandesh = sandesh_instance
        module = Module.FABRIC_ANSIBLE
        module_pkg = "job_manager"
        self.context = "job_manager"
        super(JobLogger, self).__init__(
            module, module_pkg, args, http_server_port)

    def sandesh_init(self, http_server_port=None):
        if self._sandesh is not None:
            return

        self._sandesh = Sandesh()
        self.redefine_sandesh_handles()
        if not http_server_port:
            http_server_port = self._args.http_server_port

        self._instance_id = self.sandesh_instance_id

        self._sandesh.init_generator(
            self._module_name, self._hostname, self._node_type_name,
            self.sandesh_instance_id, self._args.random_collectors,
            '%s_context' % self.context, int(http_server_port),
            ['cfgm_common'],
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf,
            config=self._args.sandesh_config)

        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)

        VncGreenlet.register_sandesh_handler()

        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)
