#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import psutil

from sandesh.analytics.ttypes import *
from sandesh.analytics.cpuinfo.ttypes import *


class CpuInfoData(object):

    def __init__(self):
        self._process = psutil.Process(os.getpid())
        self._num_cpu = 0
    #end __init__

    def _get_num_cpu(self):
        return psutil.NUM_CPUS
    #end _get_num_cpu

    def _get_sys_mem_info(self):
        phymem_info = psutil.phymem_usage()
        sys_mem_info = SysMemInfo()
        sys_mem_info.total = phymem_info[0]/1024
        sys_mem_info.used = phymem_info[1]/1024
        sys_mem_info.free = phymem_info[2]/1024
        return sys_mem_info
    #end _get_sys_mem_info

    def _get_mem_info(self):
        mem_info = MemInfo()
        mem_info.virt = self._process.get_memory_info().vms/1024
        mem_info.peakvirt = mem_info.virt
        mem_info.res = self._process.get_memory_info().rss/1024
        return mem_info
    #end _get_mem_info

    def _get_cpu_load_avg(self):
        load_avg = os.getloadavg()
        cpu_load_avg = CpuLoadAvg()
        cpu_load_avg.one_min_avg = load_avg[0]
        cpu_load_avg.five_min_avg = load_avg[1]
        cpu_load_avg.fifteen_min_avg = load_avg[2]
    #end _get_cpu_load_avg

    def _get_cpu_share(self):
        cpu_percent = self._process.get_cpu_percent(interval=0.1)
        return cpu_percent/self._get_num_cpu()
    #end _get_cpu_share

    def get_cpu_info(self, system=True):
        cpu_info = CpuLoadInfo()
        num_cpu = self._get_num_cpu()
        if self._num_cpu != num_cpu:
            self._num_cpu = num_cpu
            cpu_info.num_cpu = num_cpu
        if system:
            cpu_info.sys_mem_info = self._get_sys_mem_info()
            cpu_info.cpuload = self._get_cpu_load_avg()
        cpu_info.meminfo = self._get_mem_info()
        cpu_info.cpu_share = self._get_cpu_share()
        return cpu_info
    #end get_cpu_info

#end class CpuInfoData
