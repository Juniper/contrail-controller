#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import psutil
import time

from sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo

class WindowsProcessMemCpuUsageData(object):
    def __init__(self, pid, last_cpu, last_time):
        self.last_cpu = last_cpu
        self.last_time = last_time
        self.pid = pid

    def _get_process_cpu_share(self, current_cpu):
        last_cpu = self.last_cpu
        last_time = self.last_time

        current_time = time.time()
        interval_time = 0
        if last_cpu and (last_time != 0):
            usage_time = (current_cpu.system - last_cpu.system) + (current_cpu.user - last_cpu.user)
            interval_time = current_time - last_time

        self.last_cpu = current_cpu
        self.last_time = current_time

        if interval_time > 0:
            usage_percent = 100 * usage_time / interval_time
            cpu_share = round(usage_percent / psutil.cpu_count(), 2)
            return cpu_share
        else:
            return 0

    def get_process_mem_cpu_info(self):
        process_mem_cpu = ProcessCpuInfo()
        p = psutil.Process(self.pid)
        process_mem_cpu.cpu_share = self._get_process_cpu_share(p.cpu_times())
        process_mem_cpu.mem_virt = p.memory_info().vms / 1024
        process_mem_cpu.mem_res = p.memory_info().rss / 1024
        return process_mem_cpu
