#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys
import os
import docker

from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo


class DockerMemCpuUsageData(object):
    def __init__(self, _id, last_cpu, last_time):
        self.last_cpu = last_cpu
        self.last_time = last_time
        self.client = docker.from_env()
        if hasattr(self.client, 'api'):
            self.client = self.client.api
        self._id = format(_id, 'x').zfill(64)

    def _get_container_stats(self):
        if sys.version_info[0] == 3:
            return self.client.stats(self._id, stream=False)
        else:
            return self.client.stats(self._id, decode=True, stream=False)

    def _get_process_cpu_share(self, current_cpu):
        # sometimes docker returns empty arrays
        if "cpu_usage" not in current_cpu or "percpu_usage" not in current_cpu["cpu_usage"]:
            return 0

        last_cpu = self.last_cpu
        last_time = self.last_time
        current_time = os.times()[4]
        cpu_count = len(current_cpu["cpu_usage"]["percpu_usage"])

        # docker returns current/previous cpu stats in call
        # but it previous data can't be used cause we don't know who calls
        # stat previously
        interval_time = 0
        if last_cpu and (last_time != 0):
            sys_time = float(current_cpu['cpu_usage']['usage_in_kernelmode']
                             - last_cpu['cpu_usage']['usage_in_kernelmode']) / 1e9
            usr_time = float(current_cpu['cpu_usage']['usage_in_usermode']
                             - last_cpu['cpu_usage']['usage_in_usermode']) / 1e9
            interval_time = current_time - last_time

        self.last_cpu = current_cpu
        self.last_time = current_time

        if interval_time > 0:
            sys_percent = 100 * sys_time // interval_time
            usr_percent = 100 * usr_time // interval_time
            cpu_share = round((sys_percent + usr_percent) / cpu_count, 2)
            return cpu_share

        return 0

    def get_process_mem_cpu_info(self):
        stats = self._get_container_stats()
        cpu_stats = stats['cpu_stats']
        mem_stats = stats['memory_stats']
        process_mem_cpu = ProcessCpuInfo()
        process_mem_cpu.cpu_share = self._get_process_cpu_share(cpu_stats)
        process_mem_cpu.mem_virt = mem_stats.get('usage', 0) // 1024
        process_mem_cpu.mem_res = mem_stats.get('stats', dict()).get('rss', 0) // 1024
        return process_mem_cpu
