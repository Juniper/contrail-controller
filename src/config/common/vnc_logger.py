# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Logger for config services
"""

import datetime
import logging
import socket
import cStringIO

from cfgm_common.utils import cgitb_hook

from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from sandesh_common.vns.constants import (
        ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT)

from pysandesh.connection_info import ConnectionState
from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, \
    NodeStatus


class ConfigServiceLogger(object):

    _LOGGER_LEVEL_TO_SANDESH_LEVEL = {
        logging.CRITICAL: SandeshLevel.SYS_EMERG,
        logging.CRITICAL: SandeshLevel.SYS_ALERT,
        logging.CRITICAL: SandeshLevel.SYS_CRIT,
        logging.ERROR: SandeshLevel.SYS_ERR,
        logging.WARNING: SandeshLevel.SYS_WARN,
        logging.WARNING: SandeshLevel.SYS_NOTICE,
        logging.INFO: SandeshLevel.SYS_INFO,
        logging.DEBUG: SandeshLevel.SYS_DEBUG
    }

    def __init__(self, discovery, module, module_pkg, args=None,
                 http_server_port=None):
        self.discovery = discovery
        self.module_pkg = module_pkg
        if not hasattr(self, 'context'):
            self.context = module_pkg
        self._args = args

        node_type = Module2NodeType[module]
        self._module_name = ModuleNames[module]
        self._node_type_name = NodeTypeNames[node_type]
        self.table = "ObjectConfigNode"
        self._instance_id = INSTANCE_ID_DEFAULT
        self._hostname = socket.gethostname()

        # sandesh init
        self.sandesh_init(http_server_port)

    def _get_sandesh_logger_level(self, sandesh_level):
        return self._LOGGER_LEVEL_TO_SANDESH_LEVEL[sandesh_level]

    def log(self, log_msg, level=SandeshLevel.SYS_DEBUG, fun=None):
        if fun:
            log = fun(level=level, og_msg=log_msg, sandesh=self._sandesh)
            log.send(sandesh=self._sandesh)
        else:
            self._sandesh.logger().log(
                    SandeshLogger.get_py_logger_level(level), log_msg)

    def emergency(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_EMERG, fun=log_fun)

    def alert(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_ALERT, fun=log_fun)

    def critical(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_CRIT, fun=log_fun)

    def error(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_ERR, fun=log_fun)

    def cgitb_error(self):
        string_buf = cStringIO.StringIO()
        cgitb_hook(file=string_buf, format="text")
        self.error(string_buf.getvalue())

    def warning(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_WARN, fun=log_fun)

    def notice(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_NOTICE, fun=log_fun)

    def info(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_INFO, fun=log_fun)

    def debug(self, log_msg, log_fun=None):
        self.log(log_msg, level=SandeshLevel.SYS_DEBUG, fun=log_fun)

    def _utc_timestamp_usec(self):
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now - epoch
        return (delta.microseconds +
                (delta.seconds + delta.days * 24 * 3600) * 10 ** 6)

    def redefine_sandesh_handles(self):
        """ Redefine sandesh handle requests for various object types. """
        pass

    def sandesh_init(self, http_server_port=None):
        """ Init sandesh """
        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit value
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit(
                self._args.sandesh_send_rate_limit)
        self.redefine_sandesh_handles()
        if not http_server_port:
            http_server_port = self._args.http_server_port
        self._sandesh.init_generator(
            self._module_name, self._hostname, self._node_type_name,
            self._instance_id, self._args.collectors,
            '%s_context' % self.context, int(http_server_port),
            ['cfgm_common', '%s.sandesh' % self.module_pkg], self.discovery,
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf)

        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)

        # connection state init
        ConnectionState.init(
                self._sandesh, self._hostname, self._module_name,
                self._instance_id,
                staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus, self.table)

    def introspect_init(self):
        self._sandesh.run_introspect_server(int(self._args.http_server_port))
