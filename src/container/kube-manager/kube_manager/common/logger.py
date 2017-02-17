# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Contrail Kube Manager logger
"""

import logging
import socket

from vnc_api.vnc_api import *

from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, NodeStatus
from kube_manager.sandesh.kube_manager import ttypes as sandesh
from kube_manager.sandesh.kube_introspect import ttypes as introspect
from kube_manager.common.kube_config_db import (
         PodKM, NamespaceKM, ServiceKM, NetworkPolicyKM, IngressKM)
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.sandesh_logger import SandeshLogger
from sandesh_common.vns.constants import (
        ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT)
from sandesh_common.vns.ttypes import Module

class KubeManagerLogger(object):

    def __init__(self, args=None):
        self._args = args

        # Initialize module parameters.
        self._module = {}
        self._module["id"] = Module.KUBE_MANAGER
        self._module["name"] = ModuleNames[self._module["id"]]
        self._module["node_type"] = Module2NodeType[self._module["id"]]
        self._module["node_type_name"] =\
            NodeTypeNames[self._module["node_type"]]
        self._module["hostname"] = socket.gethostname()
        self._module["table"] = "ObjectConfigNode"
        if self._args.worker_id:
            self._module["instance_id"] = self._args.worker_id
        else:
            self._module["instance_id"] = INSTANCE_ID_DEFAULT

        # Init Sandesh.
        self.sandesh_init()

    def syslog(self, log_msg, level):
        """ Log to syslog. """
        self._sandesh.logger().log(
            SandeshLogger.get_py_logger_level(level), log_msg)

    def log(self, log_msg, level=SandeshLevel.SYS_DEBUG, fun=None):
        """
        If a sandesh function is provided, use the function.
        If not, revert to syslog.
        """
        if fun:
            log = fun(level=level, log_msg=log_msg, sandesh=self._sandesh)
            log.send(sandesh=self._sandesh)
        else:
            self.syslog(log_msg, level)

    # EMERGENCY.
    def emergency(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_EMERG
        logging_fun = log_fun if log_fun else sandesh.KubeManagerEmergencyLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # ALERT.
    def alert(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_ALERT
        logging_fun = log_fun if log_fun else sandesh.KubeManagerAlertLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # CRITICAL.
    def critical(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_CRIT
        logging_fun = log_fun if log_fun else sandesh.KubeManagerCriticalLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # ERROR.
    def error(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_ERR
        logging_fun = log_fun if log_fun else sandesh.KubeManagerErrorLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # WARNING.
    def warning(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_WARN
        logging_fun = log_fun if log_fun else sandesh.KubeManagerWarningLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # NOTICE.
    def notice(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_NOTICE
        logging_fun = log_fun if log_fun else sandesh.KubeManagerNoticeLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # INFO.
    def info(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_INFO
        logging_fun = log_fun if log_fun else sandesh.KubeManagerInfoLog

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # DEBUG.
    def debug(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_DEBUG
        logging_fun = log_fun if log_fun else sandesh.KubeManagerDebugLog

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    def _redefine_sandesh_handles(self):
        """ Register custom introspect handlers. """
        # Register Pod DB introspect handler.
        introspect.PodDatabaseList.handle_request =\
            PodKM.sandesh_handle_db_list_request

        # Register Namespace DB introspect handler.
        introspect.NamespaceDatabaseList.handle_request =\
            NamespaceKM.sandesh_handle_db_list_request

        # Register Service DB introspect handler.
        introspect.ServiceDatabaseList.handle_request =\
            ServiceKM.sandesh_handle_db_list_request

        # Register NetworkPolicy DB introspect handler.
        introspect.NetworkPolicyDatabaseList.handle_request =\
            NetworkPolicyKM.sandesh_handle_db_list_request

        # Register Ingress DB introspect handler.
        introspect.IngressDatabaseList.handle_request =\
            IngressKM.sandesh_handle_db_list_request

    def sandesh_init(self):
        """ Init Sandesh """
        self._sandesh = Sandesh()

        # Reset sandesh send rate limit value.
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit(
                self._args.sandesh_send_rate_limit)

        # Register custom sandesh request handlers.
        self._redefine_sandesh_handles()

        # Initialize Sandesh generator.
        self._sandesh.init_generator(
            self._module["name"], self._module["hostname"],
            self._module["node_type_name"], self._module["instance_id"],
            self._args.collectors, 'kube_manager_context',
            int(self._args.http_server_port),
            ['cfgm_common', 'kube_manager'],
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf,
            config=self._args.sandesh_config)

        # Set Sandesh logging params.
        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category, level=self._args.log_level,
            file=self._args.log_file, enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)

        # Connect to collector.
        ConnectionState.init(
            self._sandesh, self._module["hostname"], self._module["name"],
            self._module["instance_id"],
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus, self._module["table"])

