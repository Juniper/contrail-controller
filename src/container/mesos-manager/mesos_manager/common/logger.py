#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contrail Mesos Manager logger
"""

import logging
import socket

from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, NodeStatus
from mesos_manager.sandesh.mesos_manager import ttypes as sandesh
from mesos_manager.sandesh.mesos_introspect import ttypes as introspect
from mesos_manager.vnc.config_db import (VirtualMachineMM, VirtualRouterMM,
    VirtualMachineInterfaceMM, VirtualNetworkMM, InstanceIpMM, ProjectMM,
    DomainMM, NetworkIpamMM)
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.sandesh_logger import SandeshLogger
from sandesh_common.vns.constants import (ModuleNames, Module2NodeType,
        NodeTypeNames, INSTANCE_ID_DEFAULT, HttpPortMesosManager)
from sandesh_common.vns.ttypes import Module


class MesosManagerLogger(object):

    def __init__(self, args=None):
        self._args = args

        # Initialize module parameters.
        self.module = {}
        self.module["id"] = Module.MESOS_MANAGER
        self.module["name"] = ModuleNames[self.module["id"]]
        self.module["node_type"] = Module2NodeType[self.module["id"]]
        self.module["node_type_name"] = NodeTypeNames[self.module["node_type"]]
        if 'host_ip' in self._args:
            host_ip = self._args.host_ip
        else:
            host_ip = socket.gethostbyname(socket.getfqdn())
        self.module["hostname"] = socket.getfqdn(host_ip)
        self.module["table"] = "ObjectConfigNode"
        if self._args.worker_id:
            self.module["instance_id"] = self._args.worker_id
        else:
            self.module["instance_id"] = INSTANCE_ID_DEFAULT

        # Init Sandesh.
        self.sandesh_init()

    def syslog(self, log_msg, level):
        # Log to syslog.
        self._sandesh.logger().log(
            SandeshLogger.get_py_logger_level(level), log_msg)

    def log(self, log_msg, level=SandeshLevel.SYS_DEBUG, fun=None):
        # If a sandesh function is provided, use the function.
        # If not, revert to syslog.
        if fun:
            log = fun(level=level, log_msg=log_msg, sandesh=self._sandesh)
            log.send(sandesh=self._sandesh)
        else:
            self.syslog(log_msg, level)

    # EMERGENCY.
    def emergency(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_EMERG
        logging_fun = log_fun if log_fun else sandesh.MesosManagerEmergencyLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # ALERT.
    def alert(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_ALERT
        logging_fun = log_fun if log_fun else sandesh.MesosManagerAlertLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # CRITICAL.
    def critical(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_CRIT
        logging_fun = log_fun if log_fun else sandesh.MesosManagerCriticalLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # CRITICAL.
    def error(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_ERR
        logging_fun = log_fun if log_fun else sandesh.MesosManagerErrorLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # WARNING.
    def warning(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_WARN
        logging_fun = log_fun if log_fun else sandesh.MesosManagerWarningLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # NOTICE.
    def notice(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_NOTICE
        logging_fun = log_fun if log_fun else sandesh.MesosManagerNoticeLog

        # Log to syslog.
        self.syslog(log_msg, log_level)

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # INFO.
    def info(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_INFO
        logging_fun = log_fun if log_fun else sandesh.MesosManagerInfoLog

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    # DEBUG.
    def debug(self, log_msg, log_fun=None):
        log_level = SandeshLevel.SYS_DEBUG
        logging_fun = log_fun if log_fun else sandesh.MesosManagerDebugLog

        # Log using the desired logging function.
        self.log(log_msg, level=log_level, fun=logging_fun)

    def redefine_sandesh_handles(self):
        """ Register custom introspect handlers. """

        # Register Virtual Machine DB introspect handler.
        introspect.VirtualMachineDatabaseList.handle_request = \
            VirtualMachineMM.sandesh_handle_db_list_request

        # Register Virtual Router DB introspect handler.
        introspect.VirtualRouterDatabaseList.handle_request = \
            VirtualRouterMM.sandesh_handle_db_list_request

        # Register Virtual Machine Interface DB introspect handler.
        introspect.VirtualMachineInterfaceDatabaseList.handle_request = \
            VirtualMachineInterfaceMM.sandesh_handle_db_list_request

        # Register Virtual Network DB introspect handler.
        introspect.VirtualNetworkDatabaseList.handle_request = \
            VirtualNetworkMM.sandesh_handle_db_list_request

        # Register Instance IP DB introspect handler.
        introspect.InstanceIpDatabaseList.handle_request = \
            InstanceIpMM.sandesh_handle_db_list_request

        # Register Project DB introspect handler.
        introspect.ProjectDatabaseList.handle_request = \
            ProjectMM.sandesh_handle_db_list_request

        # Register Domain DB introspect handler.
        introspect.DomainDatabaseList.handle_request = \
            DomainMM.sandesh_handle_db_list_request

        # Register NetworkIpam DB introspect handler.
        introspect.NetworkIpamDatabaseList.handle_request = \
            NetworkIpamMM.sandesh_handle_db_list_request

    def sandesh_init(self):
        """ Init Sandesh """
        self._sandesh = Sandesh()

        # Register custom sandesh request handlers.
        self.redefine_sandesh_handles()

        # Initialize Sandesh generator.
        self._sandesh.init_generator(
            self.module['name'], self.module['hostname'],
            self.module['node_type_name'], self.module['instance_id'],
            self._args.collectors, 'mesos_manager_context',
            int(self._args.http_server_port),
            ['cfgm_common', 'mesos_manager.sandesh', 'mesos_introspect.sandesh'],
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf,
            config=self._args.sandesh_config)

        # Set Sandesh logging params.
        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level, file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)

        # Connect to collector.
        ConnectionState.init(self._sandesh, self.module['hostname'],
            self.module['name'], self.module['instance_id'],
            staticmethod(ConnectionState.get_conn_state_cb),
            NodeStatusUVE, NodeStatus, self.module['table'])

    def introspect_init(self):
        self._sandesh.run_introspect_server(int(self._args.http_server_port))
