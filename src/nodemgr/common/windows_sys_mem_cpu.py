#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import psutil

from sandesh.nodeinfo.cpuinfo.ttypes import SysMemInfo, SysCpuInfo
from common_sys_mem_cpu import SysCpuShare

class WindowsSysMemCpuUsageData(object):
    def __init__(self):
        self.sys_cpu_share = SysCpuShare(self.get_num_cpu())

    def get_num_socket(self):
        return 1 # just a stub for now

    def get_num_cpu(self):
        return psutil.cpu_count()

    def get_num_core_per_socket(self):
        return psutil.cpu_count(logical=False) / self.get_num_socket()

    def get_num_thread_per_core(self):
        return psutil.cpu_count(logical=False) / psutil.cpu_count()

    def get_sys_mem_info(self, node_type):
        sys_mem_info = SysMemInfo()
        virtmem_info = psutil.virtual_memory()
        sys_mem_info.total = virtmem_info.total / 1024
        sys_mem_info.used = virtmem_info.used / 1024
        sys_mem_info.free = virtmem_info.free / 1024
        sys_mem_info.node_type = node_type
        return sys_mem_info

    def get_sys_cpu_info(self, node_type):
        sys_cpu_info = SysCpuInfo()
        sys_cpu_info.one_min_avg = psutil.cpu_percent()
        sys_cpu_info.five_min_avg = sys_cpu_info.one_min_avg
        sys_cpu_info.fifteen_min_avg = sys_cpu_info.one_min_avg
        sys_cpu_info.cpu_share = self.sys_cpu_share.get()
        sys_cpu_info.node_type = node_type
        return sys_cpu_info
