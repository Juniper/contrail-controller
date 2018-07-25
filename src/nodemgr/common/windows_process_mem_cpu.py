#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo

class WindowsProcessMemCpuUsageData(object):
    def __init__(self, _id, last_cpu, last_time):
        self.last_cpu = last_cpu
        self.last_time = last_time
        self._id = hex(_id)[2:-1].zfill(64)

    def get_process_mem_cpu_info(self):
        process_mem_cpu = ProcessCpuInfo()
        return process_mem_cpu
