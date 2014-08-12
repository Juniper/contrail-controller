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


    def sandesh_si_handle_request(self, req):
        si_resp = sandesh.ServiceInstanceListResp(si_names=[])
        if req.si_name is None:
            vm_list = self._db.virtual_machine_list()
            si_list = self._db.service_instance_list()

            #walk all vms
            for vm_uuid, si in vm_list:
                if 'done' in si:
                    continue

                #collect all ecmp instances
                sandesh_si = sandesh.ServiceInstance(
                    name=si['si_fq_str'], si_type=si['instance_type'])
                sandesh_vm_list = []
                for key, val in vm_list:
                    if val['si_fq_str'] != si['si_fq_str']:
                        continue
                    vm_str = ("%s: %s" % (val['instance_name'], key))
                    vm = sandesh.ServiceInstanceVM(
                        name=vm_str, vr_name=val.get('vrouter_name', ''))
                    sandesh_vm_list.append(vm)
                    val['done'] = True
                sandesh_si.vm_list = list(sandesh_vm_list)

                #find the vn and iip information
                for si_fq_str, si_info in si_list:
                    if si_fq_str != si['si_fq_str']:
                        continue
                    self._sandesh_populate_vn_info(si_info, sandesh_si)
                    si_info['done'] = True
                si_resp.si_names.append(sandesh_si)

            #walk all instances where vms are pending launch
            for si_fq_str, si_info in si_list:
                if 'done' in si_info.keys():
                    continue
                sandesh_si = sandesh.ServiceInstance(
                    name=si_fq_str, si_type=si_info['instance_type'])
                sandesh_si.vm_list = []
                sandesh_si.instance_name = ''
                self._sandesh_populate_vn_info(si_info, sandesh_si)
                si_resp.si_names.append(sandesh_si)

        si_resp.response(req.context())


    def _sandesh_populate_vn_info(self, si_info, sandesh_si):
        for if_str in svc_info.get_if_str_list():
            if_set = set()
            if_str_vn = if_str + '-vn'
            if not if_str_vn in si_info.keys():
                continue

            vn_fq_str = str(si_info[if_str_vn])
            vn_uuid = str(si_info[vn_fq_str])
            vn_str = ("VN [%s : %s]" % (vn_fq_str, vn_uuid))
            if_set.add(vn_str)

            iip_uuid_str = if_str + '-iip-uuid'
            if iip_uuid_str in si_info.keys():
                vn_iip_uuid = str(si_info[iip_uuid_str])
                iip_addr_str = if_str + '-iip-addr'
                vn_iip_addr = str(si_info[iip_addr_str])
                iip_str = ("IIP [%s : %s]" % (vn_iip_addr, vn_iip_uuid))
                if_set.add(iip_str)

            if if_str == svc_info.get_left_if_str():
                sandesh_si.left_vn = list(if_set)
            if if_str == svc_info.get_right_if_str():
                sandesh_si.right_vn = list(if_set)
            if if_str == svc_info.get_management_if_str():
                sandesh_si.management_vn = list(if_set)

            si_info['done'] = True


    def _utc_timestamp_usec(self):
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now - epoch
        return (delta.microseconds +
                (delta.seconds + delta.days * 24 * 3600) * 10 ** 6)


    def uve_svc_instance(self, si_fq_name_str, status=None,
                         vm_uuid=None, st_name=None, vr_name=None):
        svc_uve = UveSvcInstanceConfig(name=si_fq_name_str,
                                       deleted=False, st_name=None,
                                       vm_list=[], create_ts=None)

        if st_name:
            svc_uve.st_name = st_name
        if vm_uuid:
            svc_uve_vm = UveSvcInstanceVMConfig(uuid=vm_uuid, vr_name=vr_name)
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
