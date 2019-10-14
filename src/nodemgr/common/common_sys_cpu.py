#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import psutil


class SysCpuShare(object):
    def __init__(self, num_cpu):
        self.last_cpu = 0
        self.last_time = 0
        self.num_cpu = num_cpu

    def get(self):
        last_cpu = self.last_cpu
        last_time = self.last_time

        current_cpu = psutil.cpu_times()
        current_time = 0.00
        for i in range(0, len(current_cpu) - 1):
            current_time += current_cpu[i]

        # tracking system/user time only
        interval_time = 0
        if last_cpu and (last_time != 0):
            sys_time = current_cpu.system - last_cpu.system
            usr_time = current_cpu.user - last_cpu.user
            interval_time = current_time - last_time

        self.last_cpu = current_cpu
        self.last_time = current_time

        if interval_time == 0:
            return 0

        sys_percent = 100 * sys_time // interval_time
        usr_percent = 100 * usr_time // interval_time
        cpu_share = round((sys_percent + usr_percent) / self.num_cpu, 2)
        return cpu_share
