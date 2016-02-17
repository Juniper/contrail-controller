# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor logger
"""

import datetime
import logging
import socket

from cfgm_common import svc_info
from cfgm_common import vnc_cpu_info
from cfgm_common.uve.service_instance.ttypes import *

from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT
from sandesh.svc_mon_introspect import ttypes as sandesh

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from config_db import *

class ServiceMonitorLogger(object):

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

    def __init__(self, discovery, args=None):
        self._args = args

        module = Module.SVC_MONITOR
        node_type = Module2NodeType[module]
        self._module_name = ModuleNames[module]
        self._node_type_name = NodeTypeNames[node_type]
        self._instance_id = INSTANCE_ID_DEFAULT
        self._hostname = socket.gethostname()

        #sandesh init
        self._sandesh = self._sandesh_init(discovery)

        # connection state init
        ConnectionState.init(self._sandesh, self._hostname, self._module_name,
            self._instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus)

        #create cpu_info object to send periodic updates
        sysinfo_req = False
        cpu_info = vnc_cpu_info.CpuInfo(self._module_name,
            self._instance_id, sysinfo_req, self._sandesh, 60)
        self._cpu_info = cpu_info

    def _get_sandesh_logger_level(self, sandesh_level):
        return self._LOGGER_LEVEL_TO_SANDESH_LEVEL[sandesh_level]

    def log(self, log_msg, level=SandeshLevel.SYS_DEBUG):
        vn_log = sandesh.SvcMonitorLog(level=level,
            log_msg=log_msg, sandesh=self._sandesh)
        vn_log.send(sandesh=self._sandesh)

    def emergency(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_EMERG)

    def alert(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_ALERT)

    def critical(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_CRIT)

    def error(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_ERR)

    def warning(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_WARN)

    def notice(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_NOTICE)

    def info(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_INFO)

    def debug(self, log_msg):
        self.log(log_msg, level=SandeshLevel.SYS_DEBUG)

    def api_conn_status_update(self, status, msg=None):
        ConnectionState.update(conn_type=ConnType.APISERVER,
            name='ApiServer', status=status, message=msg,
            server_addrs=['%s:%s' % (self._args.api_server_ip,
                                     self._args.api_server_port)])

    def db_conn_status_update(self, status, servers, msg=None):
        ConnectionState.update(conn_type=ConnType.DATABASE,
            name='Database', status=status, message=msg,
            server_addrs=servers)


    def sandesh_si_handle_request(self, req):
        si_resp = sandesh.ServiceInstanceListResp(si_names=[])
        for si in ServiceInstanceSM.values():
            if req.si_name and req.si_name != si.name:
                continue

            st = ServiceTemplateSM.get(si.service_template)
            sandesh_si = sandesh.ServiceInstance(
                name=(':').join(si.fq_name), si_type=st.virtualization_type,
                si_state=si.state)

            sandesh_vm_list = []
            for vm_id in si.virtual_machines:
                vm = VirtualMachineSM.get(vm_id)
                if not vm:
                    continue
                vm_str = ("%s: %s" % (vm.name, vm.uuid))

                vr_name = 'None'
                vr = VirtualRouterSM.get(vm.virtual_router)
                if vr:
                    vr_name = vr.name

                ha_str = "active"
                if vm.index < len(si.local_preference):
                    if vm.index >= 0:
                        ha = si.local_preference[vm.index]
                        if ha and int(ha) == svc_info.get_standby_preference():
                            ha_str = "standby"
                        if ha:
                            ha_str = ha_str + ': ' + str(ha)
                    else:
                        ha_str = "unknown"

                vm = sandesh.ServiceInstanceVM(name=vm_str,
                    vr_name=vr_name, ha=ha_str)
                sandesh_vm_list.append(vm)
            sandesh_si.vm_list = list(sandesh_vm_list)

            for nic in si.vn_info:
                vn = VirtualNetworkSM.get(nic['net-id'])
                if not vn:
                    continue
                if nic['type'] == svc_info.get_left_if_str():
                    sandesh_si.left_vn = [vn.name, vn.uuid]
                if nic['type'] == svc_info.get_right_if_str():
                    sandesh_si.right_vn = [vn.name, vn.uuid]
                if nic['type'] == svc_info.get_management_if_str():
                    sandesh_si.management_vn = [vn.name, vn.uuid]

            si_resp.si_names.append(sandesh_si)

        si_resp.response(req.context())

    def _utc_timestamp_usec(self):
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now - epoch
        return (delta.microseconds +
                (delta.seconds + delta.days * 24 * 3600) * 10 ** 6)


    def uve_svc_instance(self, si_fq_name_str, status=None,
                         vms=[], st_name=None):
        svc_uve = UveSvcInstanceConfig(name=si_fq_name_str,
                                       deleted=False, st_name=None,
                                       vm_list=[], create_ts=None)

        if st_name:
            svc_uve.st_name = st_name
        for vm in vms:
            svc_uve_vm = UveSvcInstanceVMConfig(uuid=vm['uuid'])
            if vm.has_key('vr_name'):
                svc_uve_vm.vr_name = vm['vr_name']
            if vm.has_key('ha'):
                svc_uve_vm.ha = vm['ha']
            svc_uve.vm_list.append(svc_uve_vm)
        if status:
            svc_uve.status = status
            if status == 'CREATE':
                svc_uve.create_ts = self._utc_timestamp_usec()
            elif status == 'DELETE':
                svc_uve.deleted = True

        svc_log = UveSvcInstanceConfigTrace(
            data=svc_uve, sandesh=self._sandesh)
        svc_log.send(sandesh=self._sandesh)


    # init sandesh
    def _sandesh_init(self, discovery):
        sandesh_instance = Sandesh()
        # Reset the sandesh send rate limit value
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._args.sandesh_send_rate_limit)
        sandesh.ServiceInstanceList.handle_request =\
            self.sandesh_si_handle_request
        sandesh_instance.init_generator(
            self._module_name, self._hostname, self._node_type_name,
            self._instance_id, self._args.collectors, 'svc_monitor_context',
            int(self._args.http_server_port),
            ['cfgm_common', 'svc_monitor.sandesh'], discovery,
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf)

        sandesh_instance.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)
        return sandesh_instance
