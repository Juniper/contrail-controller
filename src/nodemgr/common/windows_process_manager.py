#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import time

from windows_process_mem_cpu import WindowsProcessMemCpuUsageData

class WindowsProcessInfoManager(object):
    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return WindowsProcessMemCpuUsageData(pid, last_cpu, last_time)

    def get_all_processes(self):
        return []

    def runforever(self):
        while True:
            time.sleep(5)
