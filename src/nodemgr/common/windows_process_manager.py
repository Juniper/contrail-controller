#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import psutil
import time

import common_process_manager as cpm
from windows_process_mem_cpu import WindowsProcessMemCpuUsageData


def _get_process_by_name(name):
    return next((proc for proc in psutil.process_iter() if proc.name() == name), None)

class WindowsProcessInfoManager(object):
    def __init__(self, event_handlers):
        self._event_handlers = event_handlers
        self._process_info_cache = cpm.ProcessInfoCache()

    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return WindowsProcessMemCpuUsageData(pid, last_cpu, last_time)

    def _poll_processes(self):
        agent_name = 'contrail-vrouter-agent'
        agent_process = _get_process_by_name(agent_name + '.exe')
        info = cpm.dummy_process_info(agent_name)
        if agent_process == None:
            info['statename'] = 'PROCESS_STATE_STOPPED'
        else:
            info['statename'] = 'PROCESS_STATE_RUNNING'
            info['pid'] = agent_process.pid
            info['start'] = str(int(agent_process.create_time() * 1000000))
        return [info]

    def get_all_processes(self):
        processes_infos = self._poll_processes()
        for info in processes_infos:
            self._process_info_cache.update_cache(info)
        return processes_infos

    def runforever(self):
        while True:
            processes_infos = self._poll_processes()
            for info in processes_infos:
                if self._process_info_cache.update_cache(info):
                    self._event_handlers['PROCESS_STATE'](cpm.convert_to_pi_event(info))
            time.sleep(5)
