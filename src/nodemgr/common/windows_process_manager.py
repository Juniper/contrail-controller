#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import psutil
import time

import common_process_manager as cpm
from windows_process_mem_cpu import WindowsProcessMemCpuUsageData


def _service_status_to_state(status):
    if status == 'running':
        return 'PROCESS_STATE_RUNNING'
    else:
        return 'PROCESS_STATE_STOPPED'

def _get_service_by_name(name):
    service = None
    try:
        service = psutil.win_service_get(name)
    except:
        pass
    return service

def _get_process_by_pid(pid):
    process = None
    try:
        process = psutil.Process(pid)
    except:
        pass
    return process

class WindowsProcessInfoManager(object):
    def __init__(self, event_handlers):
        self._event_handlers = event_handlers
        self._process_info_cache = cpm.ProcessInfoCache()

    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return WindowsProcessMemCpuUsageData(pid, last_cpu, last_time)

    def _poll_processes(self):
        agent_name = 'contrail-vrouter-agent'
        agent_service = _get_service_by_name(agent_name)
        info = cpm.dummy_process_info(agent_name)
        if agent_service != None:
            info['statename'] = _service_status_to_state(agent_service.status())
            if info['statename'] == 'PROCESS_STATE_RUNNING':
                info['pid'] = agent_service.pid()
                agent_process = _get_process_by_pid(info['pid'])
                if agent_process != None:
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
