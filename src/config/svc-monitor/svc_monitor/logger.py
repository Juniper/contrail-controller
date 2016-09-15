# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor logger
"""

from cfgm_common import svc_info
from cfgm_common.uve.service_instance.ttypes import UveSvcInstanceConfig,\
        UveSvcInstanceVMConfig, UveSvcInstanceConfigTrace
from cfgm_common.vnc_logger import ConfigServiceLogger

from sandesh_common.vns.ttypes import Module
from sandesh.svc_mon_introspect import ttypes as sandesh

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType

from config_db import ServiceInstanceSM, ServiceTemplateSM,\
        VirtualMachineSM, VirtualRouterSM, VirtualNetworkSM


class ServiceMonitorLogger(ConfigServiceLogger):

    def __init__(self, discovery, args=None):
        module = Module.SVC_MONITOR
        module_pkg = 'svc_monitor'
        super(ServiceMonitorLogger, self).__init__(
                discovery, module, module_pkg, args)

    def log(self, log_msg, level=SandeshLevel.SYS_DEBUG, fun=None):
        if fun:
            vn_log = fun(level=level, log_msg=log_msg, sandesh=self._sandesh)
        else:
            vn_log = sandesh.SvcMonitorLog(level=level,
                                           log_msg=log_msg,
                                           sandesh=self._sandesh)
        vn_log.send(sandesh=self._sandesh)

    def redefine_sandesh_handles(self):
        sandesh.ServiceInstanceList.handle_request =\
                self.sandesh_si_handle_request
