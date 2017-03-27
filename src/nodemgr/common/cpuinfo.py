#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import psutil
import subprocess

from subprocess import Popen, PIPE
from sandesh.nodeinfo.cpuinfo.ttypes import *

class MemCpuUsageData(object):

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
    #end __init__

    def get_num_socket(self):
        cmd = 'lscpu | grep "Socket(s):" | awk \'{print $2}\''
        proc = Popen(cmd, shell=True, stdout=PIPE)
        return int(proc.communicate()[0])
    #end get_num_socket

    def get_num_cpu(self):
        cmd = 'lscpu | grep "^CPU(s):" | awk \'{print $2}\''
        proc = Popen(cmd, shell=True, stdout=PIPE)
        return int(proc.communicate()[0])
    #end get_num_cpu

    def get_num_core_per_socket(self):
        cmd = 'lscpu | grep "Core(s) per socket:" | awk \'{print $4}\''
        proc = Popen(cmd, shell=True, stdout=PIPE)
        return int(proc.communicate()[0])
    #end get_num_core_per_socket

    def get_num_thread_per_core (self):
        cmd = 'lscpu | grep "Thread(s) per core:" | awk \'{print $4}\''
        proc = Popen(cmd, shell=True, stdout=PIPE)
        return int(proc.communicate()[0])
    #end get_num_thread_per_core

    def get_sys_mem_info(self, node_type):
        virtmem_info = psutil.virtual_memory()
        sys_mem_info = SysMemInfo()
        sys_mem_info.total = virtmem_info.total/1024
        sys_mem_info.used = virtmem_info.used/1024
        sys_mem_info.free = virtmem_info.free/1024
        sys_mem_info.buffers = virtmem_info.buffers/1024
        sys_mem_info.cached = virtmem_info.cached/1024
        sys_mem_info.node_type = node_type
        return sys_mem_info
    #end get_sys_mem_info

    def get_sys_cpu_info(self, node_type):
        cpu_load_avg = self._get_cpu_load_avg()
        sys_cpu_info = SysCpuInfo()
        sys_cpu_info.one_min_avg = cpu_load_avg.one_min_avg
        sys_cpu_info.five_min_avg = cpu_load_avg.five_min_avg
        sys_cpu_info.fifteen_min_avg = cpu_load_avg.fifteen_min_avg
        sys_cpu_info.cpu_share = self._get_sys_cpu_share()
        sys_cpu_info.node_type = node_type
        return sys_cpu_info
    #end get_sys_cpu_info

    def _get_cpu_load_avg(self):
        load_avg = os.getloadavg()
        cpu_load_avg = CpuLoadAvg()
        cpu_load_avg.one_min_avg = load_avg[0]
        cpu_load_avg.five_min_avg = load_avg[1]
        cpu_load_avg.fifteen_min_avg = load_avg[2]
        return cpu_load_avg
    #end _get_cpu_load_avg

    def _get_sys_cpu_share(self):
        last_cpu = self.last_cpu
        last_time = self.last_time

        current_cpu = psutil.cpu_times()
        current_time = 0.00
        for i in range(0, len(current_cpu)-1):
            current_time += current_cpu[i]

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
            cpu_share = round((sys_percent + usr_percent)/self.get_num_cpu(), 2)
            return cpu_share
        else:
            return 0
    #end _get_sys_cpu_share

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
            cpu_share = round((sys_percent + usr_percent)/self.get_num_cpu(), 2)
            return cpu_share
        else:
            return 0
    #end _get_process_cpu_share

    def get_process_mem_cpu_info(self):
        process_mem_cpu           = ProcessCpuInfo()
        process_mem_cpu.cpu_share = self._get_process_cpu_share()
        process_mem_cpu.mem_virt  = self._process.get_memory_info().vms/1024
        process_mem_cpu.mem_res   = self._process.get_memory_info().rss/1024
        return process_mem_cpu
    #end get_process_mem_cpu_info
#end class MemCpuUsageData
