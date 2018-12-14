#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import gevent
import json
import ConfigParser
from StringIO import StringIO
from ConfigParser import NoOptionError, NoSectionError
import sys
import os
import psutil
import socket
import time
import subprocess
from subprocess import Popen, PIPE
import xmlrpclib
import platform
import random
import hashlib
import select
import copy
try:
    import pydbus
    pydbus_present = True
except:
    pydbus_present = False

try:
    import supervisor.xmlrpc
    from supervisor import childutils
    supervisor_event_listener_cls_type = childutils.EventListenerProtocol
except:
    supervisor_event_listener_cls_type = object

from functools import partial
from nodemgr.common.process_stat import ProcessStat
from nodemgr.common.sandesh.nodeinfo.ttypes import *
from nodemgr.common.sandesh.supervisor_events.ttypes import *
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.ttypes import ProcessState, \
    ProcessStatus, ProcessInfo, DiskPartitionUsageStats
from nodemgr.common.sandesh.nodeinfo.process_info.constants import \
    ProcessStateNames
from nodemgr.common.cpuinfo import MemCpuUsageData
from sandesh_common.vns.constants import INSTANCE_ID_DEFAULT, \
    ServiceHttpPortMap, ModuleNames, Module2NodeType, NodeTypeNames, \
    UVENodeTypeNames
from buildinfo import build_info
from pysandesh.sandesh_base import Sandesh, SandeshConfig
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.connection_info import ConnectionState
from pysandesh.util import UTCTimestampUsec
from nodemgr.utils import NodeMgrUtils

class SupervisorEventListener(supervisor_event_listener_cls_type):
    def __init__(self):
        self.supervisor_events_ctr = 0
        self.supervisor_events_timestamp = None
        self.supervisor_events_error_ctr = 0
        self.supervisor_events_error_timestamp = None

    def wait(self, stdin=sys.stdin, stdout=sys.stdout):
        self.ready(stdout)
        while 1:
            if select.select([stdin], [], [])[0]:
                line = stdin.readline()
                if line is not None:
                    self.supervisor_events_ctr += 1
                    self.supervisor_events_timestamp = str(UTCTimestampUsec())
                    break
                else:
                    self.supervisor_events_error_ctr += 1
                    self.supervisor_events_error_timestamp = str(UTCTimestampUsec)
        headers = childutils.get_headers(line)
        payload = stdin.read(int(headers['len']))
        return headers, payload
    # end wait

# end class SupervisorEventListenerProtocol

class SupervisorProcessInfoManager(object):
    def __init__(self, stdin, stdout, server_url, event_handlers,
            update_process_list = False):
        self._stdin = stdin
        self._stdout = stdout
        # ServerProxy won't allow us to pass in a non-HTTP url,
        # so we fake the url we pass into it and always use the transport's
        # 'serverurl' to figure out what to attach to
        self._proxy = xmlrpclib.ServerProxy('http://127.0.0.1',
            transport = supervisor.xmlrpc.SupervisorTransport(
                serverurl = server_url))
        self._event_listener = SupervisorEventListener()
        self._event_handlers = event_handlers
        self._update_process_list = update_process_list
    # end __init__

    def GetAllProcessInfo(self):
        """
            Supervisor XMLRPC API returns an array of per process information:
            {'name':           'process name',
             'group':          'group name',
             'description':    'pid 18806, uptime 0:03:12'
             'start':          1200361776,
             'stop':           0,
             'now':            1200361812,
             'state':          1,
             'statename':      'RUNNING',
             'spawnerr':       '',
             'exitstatus':     0,
             'logfile':        '/path/to/stdout-log', # deprecated, b/c only
             'stdout_logfile': '/path/to/stdout-log',
             'stderr_logfile': '/path/to/stderr-log',
             'pid':            1}
        """
        process_infos = self._proxy.supervisor.getAllProcessInfo()
        for process_info in process_infos:
           process_info['statename'] = 'PROCESS_STATE_' + process_info['statename']
           if 'start' in process_info:
               process_info['start'] = process_info['start'] * 1000000
        return process_infos
    # end GetAllProcessInfo

    def Run(self, test):
        while True:
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self._event_listener.wait(
                self._stdin, self._stdout)
            pheaders, pdata = childutils.eventdata(payload + '\n')
            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                """
                    eventname:PROCESS_STATE_STARTING processname:cat groupname:cat from_state:STOPPED tries:0
                    eventname:PROCESS_STATE_RUNNING processname:cat groupname:cat from_state:STARTING pid:2766
                    eventname:PROCESS_STATE_BACKOFF processname:cat groupname:cat from_state:STOPPED tries:0
                    eventname:PROCESS_STATE_STOPPING processname:cat groupname:cat from_state:STARTING pid:2766
                    eventname:PROCESS_STATE_EXITED processname:cat groupname:cat from_state:RUNNING expected:0 pid:2766
                    eventname:PROCESS_STATE_STOPPED processname:cat groupname:cat from_state:STOPPING pid:2766
                    eventname:PROCESS_STATE_FATAL processname:cat groupname:cat from_state:BACKOFF
                    eventname:PROCESS_STATE_UNKNOWN processname:cat groupname:cat from_state:BACKOFF
                """
                process_info = {}
                process_info['name'] = pheaders['processname']
                process_info['group'] = pheaders['groupname']
                process_info['state'] = headers['eventname']
                if 'pid' in pheaders:
                    process_info['pid'] = pheaders['pid']
                if 'expected' in pheaders:
                    process_info['expected'] = int(pheaders['expected'])
                self._event_handlers['PROCESS_STATE'](process_info)
                if self._update_process_list:
                    self._event_handlers['PROCESS_LIST_UPDATE']()
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self._event_handlers['PRCOESS_COMMUNICATION'](pdata)
            self._event_listener.ok(self._stdout)
    # end Run

#end class SupervisorProcessInfoManager

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
        state_mapping = { 'active' : 'PROCESS_STATE_RUNNING',
                          'failed' : 'PROCESS_STATE_EXITED',
                          'activating' : 'PROCESS_STATE_STARTING',
                          'deactivating' : 'PROCESS_STATE_STOPPING',
                          'inactive' : 'PROCESS_STATE_STOPPED',
                          'reloading' : 'PROCESS_STATE_BACKOFF',
                        }
        return state_mapping.get(active_state, 'PROCESS_STATE_UNKNOWN')
    # end GetProcessStateName

# end class SystemdActiveState

class SystemdProcessInfoManager(object):

    def __init__(self, unit_names, event_handlers, update_process_list,
            poll):
        self._poll = poll
        self._unit_paths = { unit_name : \
            SystemdUtils.UNIT_PATH_PREFIX + \
            SystemdUtils.make_path(unit_name) for unit_name in unit_names }
        self._bus = pydbus.SystemBus()
        self._event_handlers = event_handlers
        self._update_process_list = update_process_list
        self._units = {  unit_name : self._bus.get( \
            SystemdUtils.SYSTEMD_BUS_NAME, \
            unit_path) for unit_name, unit_path in self._unit_paths.items() }
        self._cached_process_infos = {}
    # end __init__

    def GetAllProcessInfo(self):
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
    # end GetAllProcessInfo

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

    def _PollProcessInfos(self):
        for unit_name, unit in self._units.items():
            process_info = self._GetProcessInfo(unit_name, unit)
            call_handlers = self._DoUpdateCachedProcessInfo(process_info)
            if call_handlers:
                self._event_handlers['PROCESS_STATE'](process_info)
                if self._update_process_list:
                    self._event_handlers['PROCESS_LIST_UPDATE']()
    # end _PollProcessInfos

    def Run(self, test):
        if self._poll:
            while True:
                self._PollProcessInfos()
                gevent.sleep(seconds=5)
        else:
            for unit_name, unit in self._units.items():
                unit_properties_changed_cb = partial(self._UnitPropertiesChanged,
                    unit_name = unit_name)
                unit.PropertiesChanged.connect(unit_properties_changed_cb)
            while True:
                gevent.sleep(seconds=0.05)
    # end Run

#end class SystemdProcessInfoManager

def package_installed(pkg):
    (pdist, _, _) = platform.dist()
    if pdist == 'Ubuntu':
        cmd = "dpkg -l " + pkg + " | " + 'grep "^ii"'
    else:
        cmd = "rpm -q " + pkg
    with open(os.devnull, "w") as fnull:
        return (not subprocess.call(cmd, stdout=fnull, stderr=fnull, shell=True))
# end package_installed

def is_systemd_based():
    (pdist, version, name) = platform.dist()
    if pdist == 'Ubuntu' and version == '16.04':
        return True
    return False
# end is_systemd_based

def is_running_in_docker():
    with open('/proc/1/cgroup', 'rt') as ifh:
        return 'docker' in ifh.read()
# end is_running_in_docker

class EventManagerTypeInfo(object):
    def __init__(self, package_name, module_type, object_table,
            supervisor_serverurl, third_party_processes = {},
            sandesh_packages = [], unit_names = []):
        self._package_name = package_name
        self._module_type = module_type
        self._module_name = ModuleNames[self._module_type]
        self._object_table = object_table
        self._node_type = Module2NodeType[self._module_type]
        self._node_type_name = NodeTypeNames[self._node_type]
        self._uve_node_type = UVENodeTypeNames[self._node_type]
        self._supervisor_serverurl = supervisor_serverurl
        self._third_party_processes = third_party_processes
        self._sandesh_packages = sandesh_packages
        self._unit_names = unit_names
    # end __init__

# end class EventManagerTypeInfo

class EventManager(object):
    group_names = []
    process_state_db = {}
    third_party_process_state_db = {}
    FAIL_STATUS_DUMMY = 0x1
    FAIL_STATUS_DISK_SPACE = 0x2
    FAIL_STATUS_SERVER_PORT = 0x4
    FAIL_STATUS_NTP_SYNC = 0x8
    FAIL_STATUS_DISK_SPACE_NA = 0x10

    def __init__(self, config, type_info, rule_file, sandesh_instance,
                 update_process_list=False):
        self.config = config
        self.type_info = type_info
        self.stdin = sys.stdin
        self.stdout = sys.stdout
        self.stderr = sys.stderr
        self.rule_file = rule_file
        self.rules_data = {'Rules':[]}
        self.max_cores = 4
        self.max_old_cores = 3
        self.max_new_cores = 1
        self.all_core_file_list = []
        self.core_dir_modified_time = 0
        self.tick_count = 0
        self.fail_status_bits = 0
        self.prev_fail_status_bits = 1
        self.instance_id = INSTANCE_ID_DEFAULT
        self.collector_addr = self.config.collectors
        self.sandesh_instance = sandesh_instance
        self.curr_build_info = None
        self.new_build_info = None
        self.last_cpu = None
        self.last_time = 0
        self.installed_package_version = None
        SupervisorEventsReq.handle_request = self.sandesh_supervisor_handle_request
        event_handlers = {}
        event_handlers['PROCESS_STATE'] = self.event_process_state
        event_handlers['PROCESS_COMMUNICATION'] = \
            self.event_process_communication
        event_handlers['PROCESS_LIST_UPDATE'] = self.update_current_process
        ConnectionState.init(self.sandesh_instance, socket.gethostname(),
            self.type_info._module_name, self.instance_id,
            staticmethod(ConnectionState.get_conn_state_cb),
            NodeStatusUVE, NodeStatus, self.type_info._object_table,
            self.get_process_state_cb)
        self.sandesh_instance.init_generator(
            self.type_info._module_name, socket.gethostname(),
            self.type_info._node_type_name, self.instance_id,
            self.collector_addr, self.type_info._module_name,
            ServiceHttpPortMap[self.type_info._module_name],
            ['nodemgr.common.sandesh'] + self.type_info._sandesh_packages,
            config=SandeshConfig.from_parser_arguments(self.config))
        self.sandesh_instance.set_logging_params(
            enable_local_log=self.config.log_local,
            category=self.config.log_category,
            level=self.config.log_level,
            file=self.config.log_file,
            enable_syslog=self.config.use_syslog,
            syslog_facility=self.config.syslog_facility)
        self.logger = self.sandesh_instance.logger()
        if is_systemd_based():
            if not pydbus_present:
                self.msg_log('Node manager cannot run without pydbus', SandeshLevel.SYS_ERR)
                exit(-1)
            # In docker, systemd notifications via sd_notify do not
            # work, hence we will poll the process status
            self.process_info_manager = SystemdProcessInfoManager(
                self.type_info._unit_names, event_handlers,
                update_process_list, is_running_in_docker())
        else:
            if not 'SUPERVISOR_SERVER_URL' in os.environ:
                self.msg_log('Node manager must be run as a supervisor event listener',
                              SandeshLevel.SYS_ERR)
                exit(-1)
            self.process_info_manager = SupervisorProcessInfoManager(
                self.stdin, self.stdout, self.type_info._supervisor_serverurl,
                event_handlers, update_process_list)
        self.add_current_process()
        for group in self.process_state_db:
            self.send_init_info(group)
        self.third_party_process_dict = self.type_info._third_party_processes
    # end __init__

    def msg_log(self, msg, level):
        self.logger.log(SandeshLogger.get_py_logger_level(
                            level), msg)
    # end msg_log

    def sandesh_supervisor_handle_request(self, req):
        supervisor_resp = SupervisorEventsResp()
        if not is_systemd_based():
            supervisor_resp.supervisor_events = SupervisorEvents( \
                count = self.process_info_manager._event_listener.supervisor_events_ctr,
                last_event_timestamp = self.process_info_manager._event_listener.supervisor_events_timestamp)
            supervisor_resp.supervisor_events_error = SupervisorEvents( \
                count = self.process_info_manager._event_listener.supervisor_events_error_ctr,
                last_event_timestamp = self.process_info_manager._event_listener.supervisor_events_error_timestamp)
        supervisor_resp.response(req.context())

    def load_rules_data(self):
        if self.rule_file and os.path.isfile(self.rule_file):
            json_file = open(self.rule_file)
            self.rules_data = json.load(json_file)
    # end load_rules_data

    def process(self):
        self.load_rules_data()
    # end process

    def get_process_name(self, process_info):
        if process_info['name'] != process_info['group']:
            process_name = process_info['group'] + ":" + process_info['name']
        else:
            process_name = process_info['name']
        return process_name
    # end get_process_name

    # Get all the current processes in the node
    def get_current_process(self):
        # Add all current processes to make sure nothing misses the radar
        process_state_db = {}
        # list of all processes on the node is made here
        for proc_info in self.process_info_manager.GetAllProcessInfo():
            proc_name = self.get_process_name(proc_info)
            proc_pid = proc_info['pid']

            process_stat_ent = self.get_process_stat_object(proc_name)
            process_stat_ent.process_state = proc_info['statename']
            if 'start' in proc_info:
                process_stat_ent.start_time = str(proc_info['start'])
                process_stat_ent.start_count += 1
            process_stat_ent.pid = proc_pid
            if process_stat_ent.group not in self.group_names:
                self.group_names.append(process_stat_ent.group)
            if not process_stat_ent.group in process_state_db:
                process_state_db[process_stat_ent.group] = {}
            process_state_db[process_stat_ent.group][proc_name] = process_stat_ent
        return process_state_db
    # end get_current_process

    # Add the current processes in the node to db
    def add_current_process(self):
        self.process_state_db = self.get_current_process()
    # end add_current_process

    # In case the processes in the Node can change, update current processes
    def update_current_process(self):
        process_state_db = self.get_current_process()
        old_process_set = set(key for group in self.process_state_db for key in self.process_state_db[group])
        new_process_set = set(key for group in process_state_db for key in process_state_db[group])
        common_process_set = new_process_set.intersection(old_process_set)
        added_process_set = new_process_set - common_process_set
        deleted_process_set = old_process_set - common_process_set
        for deleted_process in deleted_process_set:
            self.delete_process_handler(deleted_process)
        for added_process in added_process_set:
            for group in process_state_db:
                if added_process in process_state_db[group]:
                    self.add_process_handler(
                        added_process, process_state_db[group][added_process])
    # end update_current_process

    # process is deleted, send state & remove it from db
    def delete_process_handler(self, deleted_process):
        for group in self.process_state_db:
            if deleted_process in self.process_state_db[group]:
                self.process_state_db[group][deleted_process].deleted = True
                self.send_process_state_db([group])
                del self.process_state_db[group][deleted_process]
                if not self.process_state_db[group]:
                    del self.process_state_db[group]
                return
    # end delete_process_handler

    # new process added, update db & send state
    def add_process_handler(self, added_process, process_info):
        group_val = process_info.group
        self.process_state_db[group_val][added_process] = process_info
        self.send_process_state_db([group_val])
    # end add_process_handler

    def get_process_state_cb(self):
        state, description = self.get_process_state(self.fail_status_bits)
        return state, description

    def check_ntp_status(self):
        ntp_status_cmd = 'ntpq -n -c pe | grep "^*"'
        proc = Popen(ntp_status_cmd, shell=True, stdout=PIPE, stderr=PIPE, close_fds=True)
        (output, errout) = proc.communicate()
        if proc.returncode != 0:
            self.fail_status_bits |= self.FAIL_STATUS_NTP_SYNC
        else:
            self.fail_status_bits &= ~self.FAIL_STATUS_NTP_SYNC
        self.send_nodemgr_process_status()

    def get_build_info(self):
        # Retrieve build_info from package/rpm and cache it
        if self.curr_build_info is None:
            command = "contrail-version contrail-nodemgr | grep contrail-nodemgr"
            version = os.popen(command).read()
            version_partials = version.split()
            if len(version_partials) < 3:
                self.msg_log('Not enough values to parse package version %s' % version,
                             SandeshLevel.SYS_ERR)
                return ""
            else:
                _, rpm_version, build_num = version_partials
            self.new_build_info = build_info + '"build-id" : "' + \
                rpm_version + '", "build-number" : "' + \
                build_num + '"}]}'
            if (self.new_build_info != self.curr_build_info):
                self.curr_build_info = self.new_build_info
        return self.curr_build_info

    def get_corefile_path(self):
        self.core_file_path = "/var/crashes"
        cat_command = "cat /proc/sys/kernel/core_pattern"
        (core_pattern, stderr) = Popen(
                cat_command.split(),
                stdout=PIPE, close_fds=True).communicate()
        if core_pattern is not None:
            dirname_cmd = "dirname " + core_pattern
            (self.core_file_path, stderr) = Popen(
                dirname_cmd.split(),
                stdout=PIPE, close_fds=True).communicate()
        return self.core_file_path.rstrip()

    def get_corefiles(self):
        try:
            # Get the core files list in the chronological order
            ls_command = "ls -1tr " + self.get_corefile_path()
            (corenames, stderr) = Popen(
                ls_command.split(),
                stdout=PIPE, close_fds=True).communicate()
        except Exception as e:
            self.msg_log('Failed to get core files: %s' % (str(e)),
                SandeshLevel.SYS_ERR)
        else:
            exception_set = {"lost+found"}
            return [self.get_corefile_path()+'/'+core for core in corenames.split() if core not in exception_set]
    # end get_corefiles

    def remove_corefiles(self, core_files):
        for core_file in core_files:
            self.msg_log('deleting core file: %s' % (core_file),
                SandeshLevel.SYS_ERR)
            try:
                os.remove(core_file)
            except OSError as e:
                self.msg_log('Failed to delete core file: %s - %s' % (core_file,
                    str(e)), SandeshLevel.SYS_ERR)
    # end remove_corefiles

    def update_process_core_file_list(self):
        ret_value = False
        corenames = self.get_corefiles()
        process_state_db_tmp = copy.deepcopy(self.process_state_db)

        for corename in corenames:
            try:
                exec_name = corename.split('.')[1]
            except IndexError:
                # Ignore the directories and the files that do not comply
                # with the core pattern
                continue
            for group in self.process_state_db:
                for key in self.process_state_db[group]:
                    if key.startswith(exec_name):
                        process_state_db_tmp[group][key].core_file_list.append(corename.rstrip())

        for group in self.process_state_db:
            for key in self.process_state_db[group]:
                if set(process_state_db_tmp[group][key].core_file_list) != set(self.process_state_db[group][key].core_file_list):
                    self.process_state_db[group][key].core_file_list = process_state_db_tmp[group][key].core_file_list
                    ret_value = True

        return ret_value
    #end update_process_core_file_list

    def send_process_state_db_base(self, group_names, ProcessInfo):
        name = socket.gethostname()
        for group in group_names:
            process_infos = []
            delete_status = True
            for key in self.process_state_db[group]:
                pstat = self.process_state_db[group][key]
                process_info = ProcessInfo()
                process_info.process_name = key
                process_info.process_state = pstat.process_state
                process_info.start_count = pstat.start_count
                process_info.stop_count = pstat.stop_count
                process_info.exit_count = pstat.exit_count
                process_info.last_start_time = pstat.start_time
                process_info.last_stop_time = pstat.stop_time
                process_info.last_exit_time = pstat.exit_time
                process_info.core_file_list = pstat.core_file_list
                process_infos.append(process_info)
                #in tor-agent case, we should use tor-agent name as uve key
                name = pstat.name
                if pstat.deleted == False:
                    delete_status = False

            if not process_infos:
                continue

            # send node UVE
            node_status = NodeStatus()
            node_status.name = name
            node_status.deleted = delete_status
            node_status.process_info = process_infos
            node_status.build_info = self.get_build_info()
            node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                            data=node_status)
	    msg = 'send_process_state_db_base: Sending UVE:' + str(node_status_uve)
            self.msg_log(msg, SandeshLevel.SYS_INFO)
            node_status_uve.send()
    # end send_process_state_db_base

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(
            group_names, ProcessInfo)
    # end send_process_state_db

    def update_all_core_file(self):
        stat_command_option = "stat --printf=%Y " + self.get_corefile_path()
        modified_time = Popen(
            stat_command_option.split(),
            stdout=PIPE, close_fds=True).communicate()
        if modified_time[0] == self.core_dir_modified_time:
            return False
        self.core_dir_modified_time = modified_time[0]
        self.all_core_file_list = self.get_corefiles()
        self.send_process_state_db(self.group_names)
        return True

    def get_process_stat_object(self, pname):
        return ProcessStat(pname)

    def send_process_state(self, process_info):
        pname = self.get_process_name(process_info)
        # update process stats
        if pname in list(key for group in self.process_state_db for key in self.process_state_db[group]):
            for group in self.process_state_db:
                if pname in self.process_state_db[group]:
                    proc_stat = self.process_state_db[group][pname]
        else:
            proc_stat = self.get_process_stat_object(pname)

        pstate = process_info['state']
        proc_stat.process_state = pstate

        send_uve = False
        if (pstate == 'PROCESS_STATE_RUNNING'):
            proc_stat.start_count += 1
            proc_stat.start_time = str(int(time.time() * 1000000))
            send_uve = True
            proc_stat.pid = int(process_info['pid'])

        if (pstate == 'PROCESS_STATE_STOPPED'):
            proc_stat.stop_count += 1
            send_uve = True
            proc_stat.stop_time = str(int(time.time() * 1000000))
            proc_stat.last_exit_unexpected = False
            proc_stat.last_cpu = None
            proc_stat.last_time = 0

        if (pstate == 'PROCESS_STATE_EXITED'):
            proc_stat.exit_count += 1
            send_uve = True
            proc_stat.exit_time = str(int(time.time() * 1000000))
            proc_stat.last_cpu = None
            proc_stat.last_time = 0
            if not process_info['expected']:
                self.msg_log('%s with pid: %s exited abnormally' %
                    (pname, process_info['pid']), SandeshLevel.SYS_ERR)
                proc_stat.last_exit_unexpected = True
                # check for core file for this exit
                find_command_option = \
                    "find " + self.get_corefile_path() + " -name core.[A-Za-z]*." + \
                    process_info['pid'] + "*"
                self.msg_log('find command option for cores: %s' %
                    (find_command_option), SandeshLevel.SYS_DEBUG)
                (corename, stderr) = Popen(
                    find_command_option.split(),
                    stdout=PIPE, close_fds=True).communicate()

                if ((corename is not None) and (len(corename.rstrip()) >= 1)):
                    self.msg_log('core file: %s' % (corename),
                        SandeshLevel.SYS_ERR)
                    # before adding to the core file list make
                    # sure that we do not have too many cores
                    self.msg_log('core_file_list: %s, max_cores: %d' %
                        (str(proc_stat.core_file_list), self.max_cores),
                        SandeshLevel.SYS_DEBUG)
                    if (len(proc_stat.core_file_list) >= self.max_cores):
                        # get rid of old cores
                        start = self.max_old_cores
                        end = len(proc_stat.core_file_list) - \
                                self.max_new_cores + 1
                        core_files_to_be_deleted = \
                            proc_stat.core_file_list[start:end]
                        self.remove_corefiles(core_files_to_be_deleted)
                        # now delete the cores from the list as well
                        del proc_stat.core_file_list[start:end]
                    # now add the new core to the core file list
                    proc_stat.core_file_list.append(corename.rstrip())
                    self.msg_log('# of cores for %s: %d' % (pname,
                        len(proc_stat.core_file_list)), SandeshLevel.SYS_DEBUG)

        send_init_uve = False
        # update process state database
        if not proc_stat.group in self.process_state_db:
            self.process_state_db[proc_stat.group] = {}
            send_init_uve = True
        self.process_state_db[proc_stat.group][pname] = proc_stat
        if not(send_uve):
            return

        if (send_uve):
            if (send_init_uve):
                self.send_init_info(proc_stat.group)
            self.send_process_state_db([proc_stat.group])

    def send_nodemgr_process_status_base(self, ProcessStateNames,
                                         ProcessState, ProcessStatus):
        if (self.prev_fail_status_bits != self.fail_status_bits):
            self.prev_fail_status_bits = self.fail_status_bits
            fail_status_bits = self.fail_status_bits
            state, description = self.get_process_state(fail_status_bits)
            conn_infos = ConnectionState._connection_map.values()
            (cb_state, cb_description) = ConnectionState.get_conn_state_cb(conn_infos)
            if (cb_state == ProcessState.NON_FUNCTIONAL):
                state = ProcessState.NON_FUNCTIONAL
            if description != '':
                description += ' '
            description += cb_description

            process_status = ProcessStatus(
                    module_id=self.type_info._module_name, instance_id=self.instance_id,
                    state=ProcessStateNames[state], description=description)
            process_status_list = []
            process_status_list.append(process_status)
            node_status = NodeStatus(name=socket.gethostname(),
                            process_status=process_status_list)
            node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                            data=node_status)
            msg = 'send_nodemgr_process_status_base: Sending UVE:' + str(node_status_uve)
            self.msg_log(msg, SandeshLevel.SYS_INFO)
            node_status_uve.send()
    # end send_nodemgr_process_status_base

    def send_nodemgr_process_status(self):
        self.send_nodemgr_process_status_base(
            ProcessStateNames, ProcessState, ProcessStatus)
    # end send_nodemgr_process_status

    def send_init_info(self, group_name):
        key = next(key for key in self.process_state_db[group_name])
        # system_cpu_info
        mem_cpu_usage_data = MemCpuUsageData(os.getpid(), self.last_cpu, self.last_time)
        sys_cpu = SystemCpuInfo()
        sys_cpu.num_socket = mem_cpu_usage_data.get_num_socket()
        sys_cpu.num_cpu = mem_cpu_usage_data.get_num_cpu()
        sys_cpu.num_core_per_socket = mem_cpu_usage_data.get_num_core_per_socket()
        sys_cpu.num_thread_per_core = mem_cpu_usage_data.get_num_thread_per_core()

        node_status = NodeStatus(
                        name=self.process_state_db[group_name][key].name,
                        system_cpu_info=sys_cpu,
                        build_info = self.get_build_info())

        # installed/running package version
        installed_package_version = \
            NodeMgrUtils.get_package_version(self.get_package_name())
        if installed_package_version is None:
            self.msg_log('Error getting %s package version' % (self.get_package_name()),
                          SandeshLevel.SYS_ERR)
            exit(-1)
        else:
            self.installed_package_version = installed_package_version
            node_status.installed_package_version = installed_package_version
            node_status.running_package_version = installed_package_version

        node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                        data=node_status)
        node_status_uve.send()

    def get_group_processes_mem_cpu_usage(self, group_name):
        process_mem_cpu_usage = {}
        for key in self.process_state_db[group_name]:
            pstat = self.process_state_db[group_name][key]
            if (pstat.process_state == 'PROCESS_STATE_RUNNING'):
                try:
                    mem_cpu_usage_data = MemCpuUsageData(pstat.pid, pstat.last_cpu, pstat.last_time)
                    process_mem_cpu = mem_cpu_usage_data.get_process_mem_cpu_info()
                except psutil.NoSuchProcess:
                    self.msg_log('NoSuchProcess: process name: %s pid:%d' % (pstat.pname, pstat.pid),
                                 SandeshLevel.SYS_ERR)
                else:
                    process_mem_cpu.__key = pstat.pname
                    process_mem_cpu_usage[process_mem_cpu.__key] = process_mem_cpu
                    pstat.last_cpu = mem_cpu_usage_data.last_cpu
                    pstat.last_time = mem_cpu_usage_data.last_time

        # walk through all processes being monitored by nodemgr,
        # not spawned by supervisord
        for pname, pattern in self.third_party_process_dict.items():
            cmd = "ps -aux | grep " + pattern + " | awk '{print $2}' | head -n1"
            proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, close_fds=True)
            stdout, stderr = proc.communicate()
            if (stdout != ''):
                pid = int(stdout.strip('\n'))
                if pname in self.third_party_process_state_db:
                    pstat = self.third_party_process_state_db[pname]
                else:
                    pstat = self.get_process_stat_object(pname)
                    pstat.pid = pid
                    self.third_party_process_state_db[pname] = pstat
                try:
                    mem_cpu_usage_data = MemCpuUsageData(pstat.pid, pstat.last_cpu, pstat.last_time)
                    process_mem_cpu = mem_cpu_usage_data.get_process_mem_cpu_info()
                except psutil.NoSuchProcess:
                    self.msg_log('NoSuchProcess: process name:%s pid:%d' % (pstat.pname, pstat.pid),
                                 SandeshLevel.SYS_ERR)
                    self.third_party_process_state_db.pop(pstat.pname)
                else:
                    process_mem_cpu.__key = pname
                    process_mem_cpu_usage[process_mem_cpu.__key] = process_mem_cpu
                    pstat.last_cpu = mem_cpu_usage_data.last_cpu
                    pstat.last_time = mem_cpu_usage_data.last_time
        return process_mem_cpu_usage

    def get_disk_usage(self):
        disk_usage_info = {}
        partition = subprocess.Popen(
            "df -PT -t ext2 -t ext3 -t ext4 -t xfs",
            shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            close_fds=True)
        for line in partition.stdout:
            if 'Filesystem' in line:
                continue
            partition_name = line.rsplit()[0]
            partition_type = line.rsplit()[1]
            partition_space_used_1k = line.rsplit()[3]
            partition_space_available_1k = line.rsplit()[4]
            disk_usage_stat = DiskPartitionUsageStats()
            try:
                disk_usage_stat.partition_type = str(partition_type)
                disk_usage_stat.__key = str(partition_name)
                disk_usage_stat.partition_space_used_1k = \
                    int(partition_space_used_1k)
                disk_usage_stat.partition_space_available_1k = \
                    int(partition_space_available_1k)
                total_disk_space = \
                    disk_usage_stat.partition_space_used_1k + \
                    disk_usage_stat.partition_space_available_1k
                disk_usage_stat.percentage_partition_space_used = \
                    int(round((float(disk_usage_stat.partition_space_used_1k)/ \
                        float(total_disk_space))*100))
            except ValueError:
                self.msg_log('Failed to get local disk space usage', SandeshLevel.SYS_ERR)
            else:
                disk_usage_info[partition_name] = disk_usage_stat
        return disk_usage_info
    # end get_disk_usage

    def get_process_state_base(self, fail_status_bits,
                               ProcessStateNames, ProcessState):
        if fail_status_bits:
            state = ProcessState.NON_FUNCTIONAL
            description = self.get_failbits_nodespecific_desc(fail_status_bits)
            if (description is ""):
                if fail_status_bits & self.FAIL_STATUS_NTP_SYNC:
                    if description != "":
                        description += " "
                    description += "NTP state unsynchronized."
        else:
            state = ProcessState.FUNCTIONAL
            description = ''
        return state, description
    # end get_process_state_base

    def get_process_state(self, fail_status_bits):
        return self.get_process_state_base(
            fail_status_bits, ProcessStateNames, ProcessState)
    # end get_process_state

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        return ""

    def event_process_state(self, process_info):
	msg = ("process:" + process_info['name'] + "," + "group:" +
		process_info['group'] + "," + "state:" + process_info['state'])
        self.msg_log(msg, SandeshLevel.SYS_DEBUG)
        self.send_process_state(process_info)
        for rules in self.rules_data['Rules']:
            if 'processname' in rules:
                if ((rules['processname'] == process_info['group']) and
                   (rules['process_state'] == process_info['state'])):
		    msg = "got a hit with:" + str(rules)
                    self.msg_log(msg, SandeshLevel.SYS_DEBUG)
                    # do not make async calls
                    try:
                        ret_code = subprocess.call(
                            [rules['action']], shell=True,
                            stdout=self.stderr, stderr=self.stderr)
                    except Exception as e:
		        msg = ('Failed to execute action: ' + rules['action'] +
				 ' with err ' + str(e))
                        self.msg_log(msg, SandeshLevel.SYS_ERR)
                    else:
                        if ret_code:
			    msg = ('Execution of action ' + rules['action'] + 
					' returned err ' + str(ret_code))
                            self.msg_log(msg, SandeshLevel.SYS_ERR)
    # end event_process_state

    def event_process_communication(self, pdata):
        flag_and_value = pdata.partition(":")
        msg = ("Flag:" + flag_and_value[0] +
                " Value:" + flag_and_value[2])
        self.msg_log(msg, SandeshLevel.SYS_DEBUG)
        for rules in self.rules_data['Rules']:
            if 'flag_name' in rules:
                if ((rules['flag_name'] == flag_and_value[0]) and
                   (rules['flag_value'].strip() == flag_and_value[2].strip())):
                    msg = "got a hit with:" + str(rules)
	            self.msg_log(msg, SandeshLevel.SYS_DEBUG)
                    cmd_and_args = ['/usr/bin/bash', '-c', rules['action']]
                    subprocess.Popen(cmd_and_args)

    def event_tick_60(self):
        self.tick_count += 1

        for group in self.process_state_db:
            key = next(key for key in self.process_state_db[group])
            # get disk usage info periodically
            disk_usage_info = self.get_disk_usage()

            # typical ntp sync time is about 5 min - first time,
            # we scan only after 10 min
            if self.tick_count >= 10:
                self.check_ntp_status()
            if self.update_process_core_file_list():
                self.send_process_state_db([group])

            process_mem_cpu_usage = self.get_group_processes_mem_cpu_usage(group)

            # get system mem/cpu usage
            system_mem_cpu_usage_data = MemCpuUsageData(os.getpid(), self.last_cpu, self.last_time)
            system_mem_usage = system_mem_cpu_usage_data.get_sys_mem_info(
                self.type_info._uve_node_type)
            system_cpu_usage = system_mem_cpu_usage_data.get_sys_cpu_info(
                self.type_info._uve_node_type)

            # update last_cpu/time after all processing is complete
            self.last_cpu = system_mem_cpu_usage_data.last_cpu
            self.last_time = system_mem_cpu_usage_data.last_time

            # send above encoded buffer
            node_status = NodeStatus(name=self.process_state_db[group][key].name,
                                     disk_usage_info=disk_usage_info,
                                     system_mem_usage=system_mem_usage,
                                     system_cpu_usage=system_cpu_usage,
                                     process_mem_cpu_usage=process_mem_cpu_usage)
            # encode other core file
            if self.update_all_core_file():
                node_status.all_core_file_list = self.all_core_file_list

            installed_package_version = \
                NodeMgrUtils.get_package_version(self.get_package_name())
            if installed_package_version is None:
                self.msg_log('Error getting %s package version' % (self.get_package_name()),
                              SandeshLevel.SYS_ERR)
                installed_package_version = "package-version-unknown"
            if (installed_package_version != self.installed_package_version):
                node_status.installed_package_version = installed_package_version
            node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                            data=node_status)
            node_status_uve.send()
        self.installed_package_version = installed_package_version

    def do_periodic_events(self):
        self.event_tick_60()

    def run_periodically(self, function, interval, *args, **kwargs):
        while True:
            before = time.time()
            function(*args, **kwargs)

            duration = time.time() - before
            if duration < interval:
                gevent.sleep(interval-duration)
            else:
                self.msg_log('function %s duration exceeded %f interval (took %f)'
                             % (function.__name__, interval, duration), SandeshLevel.SYS_ERR)
    # end run_periodically

    def runforever(self, test=False):
        self.process_info_manager.Run(test)
    # end runforever

    def get_package_name(self):
        return self.type_info._package_name
    # end get_package_name

    def nodemgr_sighup_handler(self):
        config = ConfigParser.SafeConfigParser()
        config.read(self.config_file)
        if 'COLLECTOR' in config.sections():
            try:
                collector = config.get('COLLECTOR', 'server_list')
                collector_list = collector.split()
            except ConfigParser.NoOptionError as e:
                pass

        if collector_list:
            new_chksum = hashlib.md5("".join(collector_list)).hexdigest()
            if new_chksum != self.collector_chksum:
                self.collector_chksum = new_chksum
                self.random_collectors = \
                    random.sample(collector_list, len(collector_list)) 
            # Reconnect to achieve load-balance irrespective of list
            self.sandesh_instance.reconfig_collectors(self.random_collectors)
    #end nodemgr_sighup_handler
