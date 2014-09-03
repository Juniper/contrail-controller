#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
.. attention:: Fix the license string
"""
import os
import socket
import psutil
import gevent
from uve.cfgm_cpuinfo.ttypes import *
from uve.cfgm_cpuinfo.cpuinfo.ttypes import *
from buildinfo import build_info

# CpuInfo object for config-node


class CpuInfo(object):

    def __init__(self, module_id, instance_id, sysinfo_req, sandesh,
                 time_interval, server_ip=None):
        # store cpuinfo at init
        self._module_id = module_id
        self._instance_id = instance_id 
        self._sysinfo = sysinfo_req
        self._sandesh = sandesh
        self._time_interval = time_interval
        self._rss = 0
        self._vms = 0
        self._pvms = 0
        self._load_avg = (0, 0, 0)
        self._phymem_usage = (0, 0, 0, 0)
        self._phymem_buffers = 0
        self._num_cpus = 0
        self._cpu_share = 0
        self._curr_build_info = None
        self._new_build_info = None
        self._curr_ip = server_ip
        self._new_ip = None

        # spawn a Greenlet object to do periodic collect and send.
        gevent.spawn(self.cpu_stats)
    # end __init__

    def get_config_node_ip(self):
        return self._curr_ip
    # end __get_config_ndoe_ip

    def set_config_node_ip(self, server_ip):
        self._curr_ip = server_ip
    # end __set_config_ndoe_ip

    def cpu_stats(self):
        cfg_process = psutil.Process(os.getpid())
        while True:
            # collect Vmsizes
            self._ip_change = 0
            self._build_change = 0
            rss = cfg_process.get_memory_info().rss
            if (self._rss != rss):
                self._rss = rss

            vms = cfg_process.get_memory_info().vms
            if (self._vms != vms):
                self._vms = vms

            pvms = vms
            if (pvms > self._pvms):
                self._pvms = pvms

            if self._sysinfo:
                # collect CPU Load avg
                load_avg = os.getloadavg()
                if (load_avg != self._load_avg):
                    self._load_avg = load_avg

                # collect systemmeory info
                phymem_usage = psutil.phymem_usage()
                if (phymem_usage != self._phymem_usage):
                    self._phymem_usage = phymem_usage

                phymem_buffers = psutil.phymem_buffers()
                if (phymem_buffers != self._phymem_buffers):
                    self._phymem_buffers = phymem_buffers

                if (self._new_ip != self._curr_ip):
                    self._new_ip = self.get_config_node_ip()
                    self._ip_change = 1

                # Retrieve build_info from package/rpm and cache it
                if self._curr_build_info is None:
                    command = "contrail-version contrail-config | grep 'contrail-config'"
                    version = os.popen(command).read()
                    _, rpm_version, build_num = version.split()
                    self._new_build_info = build_info + '"build-id" : "' + \
                        rpm_version + '", "build-number" : "' + \
                        build_num + '"}]}'
                if (self._new_build_info != self._curr_build_info):
                    self._curr_build_info = self._new_build_info
                    self._build_change = 1

            num_cpus = psutil.NUM_CPUS
            if (num_cpus != self._num_cpus):
                self._num_cpus = num_cpus

            cpu_percent = cfg_process.get_cpu_percent(interval=0.1)
            cpu_share = cpu_percent / num_cpus
            self._cpu_share = cpu_share

            self._send_cpustats()

            gevent.sleep(self._time_interval)
    # end cpu_stats

    # Send Uve Object
    def _send_cpustats(self):
        mod_cpu = ModuleCpuInfo()
        mod_cpu.module_id = self._module_id
        mod_cpu.instance_id = self._instance_id

        mod_cpu.cpu_info = CpuLoadInfo()

        # populate number of available CPU
        mod_cpu.cpu_info.num_cpu = self._num_cpus

        if self._sysinfo:
            # populate system memory details
            mod_cpu.cpu_info.sys_mem_info = SysMemInfo()
            mod_cpu.cpu_info.sys_mem_info.total = self._phymem_usage[0] / 1000
            mod_cpu.cpu_info.sys_mem_info.used = self._phymem_usage[1] / 1000
            mod_cpu.cpu_info.sys_mem_info.free = self._phymem_usage[2] / 1000
            mod_cpu.cpu_info.sys_mem_info.buffers = self._phymem_buffers / 1000

            # populate CPU Load avg
            mod_cpu.cpu_info.cpuload = CpuLoadAvg()
            mod_cpu.cpu_info.cpuload.one_min_avg = self._load_avg[0]
            mod_cpu.cpu_info.cpuload.five_min_avg = self._load_avg[1]
            mod_cpu.cpu_info.cpuload.fifteen_min_avg = self._load_avg[2]

        # populate Virtual Memory details
        mod_cpu.cpu_info.meminfo = MemInfo()
        mod_cpu.cpu_info.meminfo.virt = self._vms / 1000
        mod_cpu.cpu_info.meminfo.peakvirt = self._pvms / 1000
        mod_cpu.cpu_info.meminfo.res = self._rss / 1000

        # populate cpu_share, which is calibrated with num_cpu
        mod_cpu.cpu_info.cpu_share = self._cpu_share

        cpu_load_info_list = [mod_cpu]

        cfgm_cpu_uve = ModuleCpuState(module_cpu_info=cpu_load_info_list)
        cfgm_cpu_uve.name = socket.gethostname()
        if self._sysinfo:
            if self._ip_change:
                cfgm_cpu_uve.config_node_ip = self._new_ip
            if self._build_change:
                cfgm_cpu_uve.build_info = self._curr_build_info

        if (self._module_id == "ApiServer"):
            cfgm_cpu_uve.api_server_mem_virt = mod_cpu.cpu_info.meminfo.virt
            cfgm_cpu_uve.api_server_cpu_share = self._cpu_share

        if (self._module_id == "Schema"):
            cfgm_cpu_uve.schema_xmer_mem_virt = mod_cpu.cpu_info.meminfo.virt
            cfgm_cpu_uve.schema_xmer_cpu_share = self._cpu_share

        if (self._module_id == "ServiceMonitor"):
            cfgm_cpu_uve.service_monitor_mem_virt =\
                mod_cpu.cpu_info.meminfo.virt
            cfgm_cpu_uve.service_monitor_cpu_share = self._cpu_share

        cpu_info_trace = ModuleCpuStateTrace(
            data=cfgm_cpu_uve, sandesh=self._sandesh)
        cpu_info_trace.send(sandesh=self._sandesh)

        cnf_cpu_state = ConfigCpuState()
        cnf_cpu_state.name = socket.gethostname()

        cnf_cpu_info = ProcessCpuInfo()
        cnf_cpu_info.module_id =  self._module_id
        cnf_cpu_info.inst_id = self._instance_id
        cnf_cpu_info.cpu_share = self._cpu_share
        cnf_cpu_info.mem_virt = mod_cpu.cpu_info.meminfo.virt
        cnf_cpu_state.cpu_info = [cnf_cpu_info]

        cnf_cpu_state_trace = ConfigCpuStateTrace(
            sandesh=self._sandesh, data=cnf_cpu_state)
        cnf_cpu_state_trace.send(sandesh=self._sandesh)

    # end _send_cpustats

# end class CpuInfo
