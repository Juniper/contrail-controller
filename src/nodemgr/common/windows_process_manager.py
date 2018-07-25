#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import psutil
import time

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
    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return WindowsProcessMemCpuUsageData(pid, last_cpu, last_time)

    def get_all_processes(self):
        agent_service = _get_service_by_name('ContrailAgent')
        if agent_service != None:
            info = {}
            info['name'] = 'contrail-vrouter-agent'
            info['group'] = info['name']
            info['statename'] = _service_status_to_state(agent_service.status())
            if info['statename'] == 'PROCESS_STATE_RUNNING':
                info['pid'] = agent_service.pid()
                agent_process = _get_process_by_pid(info['pid'])
                if agent_process != None:
                    info['start'] = str(int(agent_process.create_time() * 1000000))
            return [info]
        else:
            return []

    def runforever(self):
        while True:
            time.sleep(5)
