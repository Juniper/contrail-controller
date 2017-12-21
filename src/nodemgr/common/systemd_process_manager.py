#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import gevent
import subprocess

try:
    import pydbus
    pydbus_present = True
except:
    pydbus_present = False

from functools import partial
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from process_mem_cpu import ProcessMemCpuUsageData


class SystemdUtils(object):
    SERVICE_IFACE = "org.freedesktop.systemd1.Service"
    UNIT_IFACE = "org.freedesktop.systemd1.Unit"

    PATH_REPLACEMENTS = {
        ".": "_2e",
        "-": "_2d",
        "/": "_2f"
    }

    SYSTEMD_BUS_NAME = "org.freedesktop.systemd1"
    UNIT_PATH_PREFIX = "/org/freedesktop/systemd1/unit/"

    @staticmethod
    def make_path(unit):
        for from_, to in \
                SystemdUtils.PATH_REPLACEMENTS.iteritems():
            unit = unit.replace(from_, to)
        return unit
        # end make_path

# end class SystemdUtils


class SystemdActiveState(object):
    # Convert systemd ActiveState to supervisord ProcessState
    @staticmethod
    def GetProcessStateName(active_state):
        # We do not use the supervisor.states.getProcessStateDescription
        # method so that we can eliminate supervisor dependency in the
        # future
        state_mapping = {'active': 'PROCESS_STATE_RUNNING',
                         'failed': 'PROCESS_STATE_EXITED',
                         'activating': 'PROCESS_STATE_STARTING',
                         'deactivating': 'PROCESS_STATE_STOPPING',
                         'inactive': 'PROCESS_STATE_STOPPED',
                         'reloading': 'PROCESS_STATE_BACKOFF',
                         }
        return state_mapping.get(active_state, 'PROCESS_STATE_UNKNOWN')
        # end GetProcessStateName

# end class SystemdActiveState


class SystemdProcessInfoManager(object):
    def __init__(self, unit_names, event_handlers, update_process_list):
        if not pydbus_present:
            self.msg_log('Node manager cannot run without pydbus', SandeshLevel.SYS_ERR)
            exit(-1)

        self._unit_paths = {unit_name:
                                SystemdUtils.UNIT_PATH_PREFIX +
                                SystemdUtils.make_path(unit_name) for unit_name in unit_names}
        self._bus = pydbus.SystemBus()
        self._event_handlers = event_handlers
        self._update_process_list = update_process_list
        self._units = {unit_name: self._bus.get(
            SystemdUtils.SYSTEMD_BUS_NAME,
            unit_path) for unit_name, unit_path in self._unit_paths.items()}
        self._cached_process_infos = {}

    # end __init__

    def get_all_processes(self):
        process_infos = []
        for unit_name, unit in self._units.items():
            process_info = {}
            assert unit_name == unit.Id
            process_name = unit_name.rsplit('.service', 1)[0]
            process_info['name'] = process_name
            process_info['group'] = process_name
            process_info['pid'] = unit.ExecMainPID
            process_info['start'] = unit.ExecMainStartTimestamp
            process_info['statename'] = SystemdActiveState.GetProcessStateName(
                unit.ActiveState)
            process_infos.append(process_info)
            cprocess_info = process_info.copy()
            cprocess_info['state'] = cprocess_info.pop('statename')
            del cprocess_info['start']
            if process_name not in self._cached_process_infos:
                self._cached_process_infos[process_name] = cprocess_info
        return process_infos

    def _UnitPropertiesChanged(self, iface, changed, invalidated, unit_name):
        if iface == SystemdUtils.UNIT_IFACE:
            process_info = {}
            process_info['name'] = unit_name.rsplit('.service', 1)[0]
            process_info['group'] = process_info['name']
            process_info['pid'] = self._units[unit_name].ExecMainPID
            process_info['state'] = SystemdActiveState.GetProcessStateName(
                changed['ActiveState'])
            if process_info['state'] == 'PROCESS_STATE_EXITED':
                process_info['expected'] = -1
            self._event_handlers['PROCESS_STATE'](process_info)
            if self._update_process_list:
                self._event_handlers['PROCESS_LIST_UPDATE']()

    # end _UnitPropertiesChanged

    def _GetProcessInfo(self, unit_name, unit):
        process_info = {}
        assert unit_name == unit.Id
        process_name = unit_name.rsplit('.service', 1)[0]
        process_info['name'] = process_name
        process_info['group'] = process_name
        process_info['pid'] = unit.ExecMainPID
        process_info['state'] = SystemdActiveState.GetProcessStateName(
            unit.ActiveState)
        if process_info['state'] == 'PROCESS_STATE_EXITED':
            process_info['expected'] = -1
        return process_info

    # end _GetProcessInfo

    def _DoUpdateCachedProcessInfo(self, process_info):
        updated = False
        process_name = process_info['name']
        if process_name not in self._cached_process_infos:
            self._cached_process_infos[process_name] = process_info
            updated = True
        else:
            cprocess_info = self._cached_process_infos[process_name]
            if cprocess_info['name'] != process_info['name'] or \
                            cprocess_info['group'] != process_info['group'] or \
                            cprocess_info['pid'] != process_info['pid'] or \
                            cprocess_info['state'] != process_info['state']:
                self._cached_process_infos[process_name] = process_info
                updated = True
        return updated

    # end _DoUpdateCachedProcessInfo

    def run(self, test):
        for unit_name, unit in self._units.items():
            unit_properties_changed_cb = partial(self._UnitPropertiesChanged,
                                                 unit_name=unit_name)
            unit.PropertiesChanged.connect(unit_properties_changed_cb)
        while True:
            gevent.sleep(seconds=0.05)
            # end Run

    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return ProcessMemCpuUsageData(pid, last_cpu, last_time)

    def find_pid(self, name, pattern):
        pid = None
        cmd = "ps -aux | grep " + pattern + " | awk '{print $2}' | head -n1"
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, close_fds=True)
        stdout, _ = proc.communicate()
        if stdout is not None and stdout != '':
            pid = int(stdout.strip('\n'))
        return pid

# end class SystemdProcessInfoManager
