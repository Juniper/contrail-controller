#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import psutil

from sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo

class ProcessMemCpuUsageData(object):
    def __init__(self, pid, last_cpu, last_time):
        self.pid = pid
        self.last_cpu = last_cpu
        self.last_time = last_time
        try:
            self._process = psutil.Process(self.pid)
        except psutil.NoSuchProcess:
            raise
        else:
            if not hasattr(self._process, 'get_memory_info'):
                self._process.get_memory_info = self._process.memory_info
        if hasattr(psutil, 'cpu_count'):
            self._cpu_count = psutil.cpu_count()
        else:
            # psutil v1.2 has no cpu_count function but has NUM_CPUS instead
            self._cpu_count = psutil.NUM_CPUS

    def _get_process_cpu_share(self):
        last_cpu = self.last_cpu
        last_time = self.last_time

        if hasattr(self._process, 'get_cpu_times'):
            current_cpu = self._process.get_cpu_times()
        else:
            current_cpu = self._process.cpu_times()
        current_time = os.times()[4]

        # tracking system/user time only
        interval_time = 0
        if last_cpu and (last_time != 0):
            sys_time = current_cpu.system - last_cpu.system
            usr_time = current_cpu.user - last_cpu.user
            interval_time = current_time - last_time

        self.last_cpu = current_cpu
        self.last_time = current_time

        if interval_time > 0:
            sys_percent = 100 * sys_time / interval_time
            usr_percent = 100 * usr_time / interval_time
            cpu_share = round((sys_percent + usr_percent) / self._cpu_count, 2)
            return cpu_share
        else:
            return 0

    def get_process_mem_cpu_info(self):
        process_mem_cpu = ProcessCpuInfo()
        process_mem_cpu.cpu_share = self._get_process_cpu_share()
        process_mem_cpu.mem_virt = self._process.get_memory_info().vms/1024
        process_mem_cpu.mem_res = self._process.get_memory_info().rss/1024
        return process_mem_cpu
