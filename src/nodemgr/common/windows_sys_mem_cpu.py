#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from sandesh.nodeinfo.cpuinfo.ttypes import SysMemInfo, SysCpuInfo

class WindowsSysMemCpuUsageData(object):
    def __init__(self, last_cpu, last_time):
        self.last_cpu = last_cpu
        self.last_time = last_time

    def get_num_socket(self):
        return 0

    def get_num_cpu(self):
        return 0

    def get_num_core_per_socket(self):
        return 0

    def get_num_thread_per_core(self):
        return 0

    def get_sys_mem_info(self, node_type):
        sys_mem_info = SysMemInfo()
        return sys_mem_info

    def get_sys_cpu_info(self, node_type):
        sys_cpu_info = SysCpuInfo()
        return sys_cpu_info
