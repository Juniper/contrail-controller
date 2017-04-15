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

    def __init__(self, discovery, args=None, http_server_port=None):
        module = Module.SVC_MONITOR
        module_pkg = 'svc_monitor'
        super(ServiceMonitorLogger, self).__init__(
                discovery, module, module_pkg, args, http_server_port)

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

    def api_conn_status_update(self, status, msg=None):
        ConnectionState.update(
                conn_type=ConnType.APISERVER, name='ApiServer', status=status,
                message=msg, server_addrs=['%s:%s' % (self._args.api_server_ip,
                                           self._args.api_server_port)])

    def db_conn_status_update(self, status, servers, msg=None):
        ConnectionState.update(
                conn_type=ConnType.DATABASE, name='Database', status=status,
                message=msg, server_addrs=servers)

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

                vm = sandesh.ServiceInstanceVM(
                        name=vm_str, vr_name=vr_name, ha=ha_str)
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
