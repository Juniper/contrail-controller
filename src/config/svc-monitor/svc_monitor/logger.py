# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor logger
"""

import datetime
import socket

from cfgm_common import svc_info
from cfgm_common import vnc_cpu_info
from cfgm_common.uve.service_instance.ttypes import *

from pysandesh.sandesh_base import Sandesh
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT
from sandesh.svc_mon_introspect import ttypes as sandesh

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType, \
    ConnectionStatus
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, \
    NodeStatus


class ServiceMonitorLogger(object):

    def __init__(self, db, discovery, args=None):
        self._args = args
        self._db = db

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


    def log(self, log_msg):
        self._sandesh._logger.debug("%s", log_msg)
        vn_log = sandesh.SvcMonitorLog(
            log_msg=log_msg, sandesh=self._sandesh)
        vn_log.send(sandesh=self._sandesh)

    
    def api_conn_status_update(self, status, msg=None):
        ConnectionState.update(conn_type=ConnectionType.APISERVER,
            name='ApiServer', status=status, message=msg,
            server_addrs=['%s:%s' % (self._args.api_server_ip,
                                     self._args.api_server_port)])

    def db_conn_status_update(self, status, servers, msg=None):
        ConnectionState.update(conn_type=ConnectionType.DATABASE,
            name='Database', status=status, message=msg,
            server_addrs=servers)


    def sandesh_si_handle_request(self, req):
        si_resp = sandesh.ServiceInstanceListResp(si_names=[])
        if req.si_name is None:
            si_list = self._db.service_instance_list()

            for si_fq_name_str, si in si_list or []:
                sandesh_si = sandesh.ServiceInstance(
                    name=si_fq_name_str, si_type=si.get('instance_type', ''),
                    si_state=si.get('state', ''))

                sandesh_vm_list = []
                for idx in range(0, int(si.get('max-instances', '0'))):
                    prefix = self._db.get_vm_db_prefix(idx)
                    vm_name = si.get(prefix + 'name', '')
                    vm_uuid = si.get(prefix + 'uuid', '')
                    vm_str = ("%s: %s" % (vm_name, vm_uuid))
                    vr_name = si.get(prefix + 'vrouter', '')
                    ha = si.get(prefix + 'preference', '')
                    if int(ha) == svc_info.get_standby_preference():
                        ha_str = ("standby: %s" % (ha))
                    else:
                        ha_str = ("active: %s" % (ha))
                    vm = sandesh.ServiceInstanceVM(name=vm_str,
                        vr_name=vr_name, ha=ha_str)
                    sandesh_vm_list.append(vm)
                sandesh_si.vm_list = list(sandesh_vm_list)

                for itf_type in svc_info.get_if_str_list():
                    key = itf_type + '-vn'
                    if key not in si.keys():
                        continue
                    vn_name = si[key]
                    vn_uuid = si[vn_name]
                    if itf_type == svc_info.get_left_if_str():
                        sandesh_si.left_vn = [vn_name, vn_uuid]
                    if itf_type == svc_info.get_right_if_str():
                        sandesh_si.right_vn = [vn_name, vn_uuid]
                    if itf_type == svc_info.get_management_if_str():
                        sandesh_si.management_vn = [vn_name, vn_uuid]

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
        sandesh.ServiceInstanceList.handle_request =\
            self.sandesh_si_handle_request
        sandesh_instance.init_generator(
            self._module_name, self._hostname, self._node_type_name, 
            self._instance_id, self._args.collectors, 'svc_monitor_context',
            int(self._args.http_server_port), ['cfgm_common', 'svc_monitor.sandesh'],
            discovery)
        sandesh_instance.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)
        return sandesh_instance
