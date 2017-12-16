#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import docker
import psutil

from sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo


class DockerMemCpuUsageData(object):
    def __init__(self, pid, last_cpu, last_time):
        self.last_cpu = last_cpu
        self.last_time = last_time
        self.client = docker.from_env()
        self._pid = hex(pid)[2:-1].zfill(64)
        self._cpu_count = psutil.cpu_count()

    def _get_container_stats(self):
        return self.client.stats(self._pid, decode=True, stream=False)

    def _get_process_cpu_share(self, current_cpu):
        last_cpu = self.last_cpu
        last_time = self.last_time

        current_time = os.times()[4]

        # tracking system/user time only
        interval_time = 0
        if last_cpu and (last_time != 0):
            sys_time = current_cpu['system_cpu_usage'] - last_cpu['system_cpu_usage']
            usr_time = current_cpu['cpu_usage']['usage_in_usermode'] - last_cpu['cpu_usage']['usage_in_usermode']
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
        stats = self._get_container_stats()
        cpu_stats = stats['cpu_stats']
        mem_stats = stats['memory_stats']
        process_mem_cpu = ProcessCpuInfo()
        process_mem_cpu.cpu_share = self._get_process_cpu_share(cpu_stats)
        process_mem_cpu.mem_virt = mem_stats['usage'] / 1024
        process_mem_cpu.mem_res = mem_stats['stats']['rss'] / 1024
        return process_mem_cpu
