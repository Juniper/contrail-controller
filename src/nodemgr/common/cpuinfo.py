#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import psutil
import subprocess

from subprocess import Popen, PIPE
from sandesh.cpuinfo.ttypes import *

class MemCpuUsageData(object):

    def __init__(self, pid):
        self.pid = pid
        try:
            self._process = psutil.Process(self.pid)
        except psutil.NoSuchProcess:
            raise
        else:
            if not hasattr(self._process, 'get_memory_info'):
                self._process.get_memory_info = self._process.memory_info
            if not hasattr(self._process, 'get_cpu_percent'):
                self._process.get_cpu_percent = self._process.cpu_percent
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

    def _get_sys_mem_info(self):
        virtmem_info = psutil.virtual_memory()
        sys_mem_info = SysMemInfo()
        sys_mem_info.total = virtmem_info.total/1024
        sys_mem_info.used = virtmem_info.used/1024
        sys_mem_info.free = virtmem_info.free/1024
        sys_mem_info.buffers = virtmem_info.buffers/1024
        sys_mem_info.cached = virtmem_info.cached/1024
        return sys_mem_info
    #end _get_sys_mem_info

    def _get_cpu_load_avg(self):
        load_avg = os.getloadavg()
        cpu_load_avg = CpuLoadAvg()
        cpu_load_avg.one_min_avg = load_avg[0]
        cpu_load_avg.five_min_avg = load_avg[1]
        cpu_load_avg.fifteen_min_avg = load_avg[2]
        return cpu_load_avg
    #end _get_cpu_load_avg

    def _get_cpu_share(self):
        cpu_percent = self._process.get_cpu_percent(interval=0.1)
        return cpu_percent/self.get_num_cpu()
    #end _get_cpu_share

    def get_sys_mem_cpu_info(self):
        sys_mem_cpu = SystemMemCpuUsage()
        sys_mem_cpu.cpu_load = self._get_cpu_load_avg()
        sys_mem_cpu.mem_info = self._get_sys_mem_info()
        sys_mem_cpu.cpu_share = self._get_cpu_share()
        return sys_mem_cpu
    #end get_sys_mem_cpu_info

    def get_process_mem_cpu_info(self):
        process_mem_cpu           = ProcessCpuInfo()
        process_mem_cpu.cpu_share = self._process.get_cpu_percent(interval=0.1)/psutil.NUM_CPUS
        process_mem_cpu.mem_virt  = self._process.get_memory_info().vms/1024
        process_mem_cpu.mem_res   = self._process.get_memory_info().rss/1024
        return process_mem_cpu
    #end get_process_mem_cpu_info
#end class MemCpuUsageData
