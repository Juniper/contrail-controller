#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
from subprocess import Popen, PIPE
import psutil

from sandesh.nodeinfo.cpuinfo.ttypes import SysMemInfo, SysCpuInfo, CpuLoadAvg
from common_sys_mem_cpu import SysCpuShare


def _run_cmd(cmd):
    proc = Popen(cmd, shell=True, stdout=PIPE, close_fds=True)
    return int(proc.communicate()[0])


class LinuxSysMemCpuUsageData(object):
    def __init__(self):
        self.sys_cpu_share = SysCpuShare(self.get_num_cpu())

    def get_num_socket(self):
        return _run_cmd('lscpu | grep "Socket(s):" '
                        '| awk \'{print $2}\'')

    def get_num_cpu(self):
        return _run_cmd('lscpu | grep "^CPU(s):" '
                        '| awk \'{print $2}\'')

    def get_num_core_per_socket(self):
        return _run_cmd('lscpu | grep "Core(s) per socket:" '
                        '| awk \'{print $4}\'')

    def get_num_thread_per_core(self):
        return _run_cmd('lscpu | grep "Thread(s) per core:" '
                        '| awk \'{print $4}\'')

    def get_sys_mem_info(self, node_type):
        virtmem_info = psutil.virtual_memory()
        sys_mem_info = SysMemInfo()
        sys_mem_info.total = virtmem_info.total / 1024
        sys_mem_info.used = virtmem_info.used / 1024
        sys_mem_info.free = virtmem_info.free / 1024
        sys_mem_info.buffers = virtmem_info.buffers / 1024
        sys_mem_info.cached = virtmem_info.cached / 1024
        sys_mem_info.node_type = node_type
        return sys_mem_info

    def get_sys_cpu_info(self, node_type):
        cpu_load_avg = self._get_sys_cpu_load_avg()
        sys_cpu_info = SysCpuInfo()
        sys_cpu_info.one_min_avg = cpu_load_avg.one_min_avg
        sys_cpu_info.five_min_avg = cpu_load_avg.five_min_avg
        sys_cpu_info.fifteen_min_avg = cpu_load_avg.fifteen_min_avg
        sys_cpu_info.cpu_share = self.sys_cpu_share.get()
        sys_cpu_info.node_type = node_type
        return sys_cpu_info

    def _get_sys_cpu_load_avg(self):
        load_avg = os.getloadavg()
        cpu_load_avg = CpuLoadAvg()
        cpu_load_avg.one_min_avg = load_avg[0]
        cpu_load_avg.five_min_avg = load_avg[1]
        cpu_load_avg.fifteen_min_avg = load_avg[2]
        return cpu_load_avg
