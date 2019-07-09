#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import ConfigParser
import copy
import gevent
import hashlib
import platform
import random
import signal
import socket
import time
from ConfigParser import NoOptionError

from buildinfo import build_info
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_base import Sandesh, SandeshConfig, sandesh_global
from pysandesh.sandesh_logger import SandeshLogger
from sandesh.nodeinfo.cpuinfo.ttypes import *
from sandesh.nodeinfo.process_info.constants import ProcessStateNames
from sandesh.nodeinfo.process_info.ttypes import (ProcessInfo, ProcessState,
                                                  ProcessStatus)
from sandesh.nodeinfo.ttypes import *
from sandesh.supervisor_events.ttypes import *
from sandesh_common.vns.constants import (INSTANCE_ID_DEFAULT, Module2NodeType,
                                          ModuleNames, NodeTypeNames,
                                          ServiceHttpPortMap, UVENodeTypeNames)

from process_stat import ProcessStat
import utils
import os
try:
    from docker_process_manager import DockerProcessInfoManager
except Exception:
    # there is no docker library. assumes that code runs not for microservices
    DockerProcessInfoManager = None
if platform.system() == 'Windows':
    from windows_sys_data import WindowsSysData
    from windows_process_manager import WindowsProcessInfoManager
else:
    from linux_sys_data import LinuxSysData


class EventManagerTypeInfo(object):
    def __init__(self, module_type, object_table, sandesh_packages=[]):
        self._module_type = module_type
        self._module_name = ModuleNames[self._module_type]
        self._object_table = object_table
        self._node_type = Module2NodeType[self._module_type]
        self._node_type_name = NodeTypeNames[self._node_type]
        self._uve_node_type = UVENodeTypeNames[self._node_type]
        self._sandesh_packages = sandesh_packages
    # end __init__
# end class EventManagerTypeInfo


class EventManager(object):
    group_names = []
    process_state_db = {}
    FAIL_STATUS_DUMMY = 0x1
    FAIL_STATUS_DISK_SPACE = 0x2
    FAIL_STATUS_SERVER_PORT = 0x4
    FAIL_STATUS_NTP_SYNC = 0x8
    FAIL_STATUS_DISK_SPACE_NA = 0x10

    def __init__(self, config, type_info, unit_names, update_process_list=False):
        self.config = config
        self.type_info = type_info
        self.max_cores = 4
        self.max_old_cores = 3
        self.max_new_cores = 1
        self.all_core_file_list = []
        self.tick_count = 0
        self.fail_status_bits = 0
        self.prev_fail_status_bits = 1
        self.instance_id = INSTANCE_ID_DEFAULT
        self.sandesh_instance = sandesh_global
        self.curr_build_info = None
        self.new_build_info = None
        self.hostip = self.config.hostip
        self.hostname = socket.getfqdn(self.hostip)

        self.collector_chksum = 0
        self.random_collectors = list()
        if config.collectors:
            config.collectors.sort()
            self.collector_chksum = hashlib.md5("".join(config.collectors)).hexdigest()
            self.random_collectors = random.sample(config.collectors, len(config.collectors))

        ConnectionState.init(self.sandesh_instance, self.hostname,
            self.type_info._module_name, self.instance_id,
            staticmethod(ConnectionState.get_conn_state_cb),
            NodeStatusUVE, NodeStatus, self.type_info._object_table,
            self._get_process_state_cb)
        self.sandesh_instance.init_generator(
            self.type_info._module_name, self.hostname,
            self.type_info._node_type_name, self.instance_id,
            self.random_collectors, self.type_info._module_name,
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

        event_handlers = {}
        event_handlers['PROCESS_STATE'] = self._event_process_state
        event_handlers['PROCESS_COMMUNICATION'] = self._event_process_communication
        event_handlers['PROCESS_LIST_UPDATE'] = self._update_current_processes
        if platform.system() == 'Windows':
            self.system_data = WindowsSysData()
            self.process_info_manager = WindowsProcessInfoManager(event_handlers)
        else:
            gevent.signal(signal.SIGHUP, self.nodemgr_sighup_handler)
            self.system_data = LinuxSysData(self.msg_log, self.config.corefile_path)
            if DockerProcessInfoManager and (utils.is_running_in_docker()
                                             or utils.is_running_in_kubepod()):
                self.process_info_manager = DockerProcessInfoManager(
                    type_info._module_type, unit_names, event_handlers,
                    update_process_list)
            else:
                self.msg_log('Node manager could not detect process manager',
                            SandeshLevel.SYS_ERR)
                exit(-1)

        self.process_state_db = self._get_current_processes()
        for group in self.process_state_db:
            self._send_init_info(group)

    def msg_log(self, msg, level):
        self.logger.log(SandeshLogger.get_py_logger_level(level), msg)

    def _get_process_name(self, process_info):
        if process_info['name'] != process_info['group']:
            process_name = process_info['group'] + ":" + process_info['name']
        else:
            process_name = process_info['name']
        return process_name

    # Get all the current processes in the node
    def _get_current_processes(self):
        # Add all current processes to make sure nothing misses the radar
        process_state_db = {}
        # list of all processes on the node is made here
        all_processes = self.process_info_manager.get_all_processes()
        self.msg_log("DBG: get_current_processes: '%s'" % all_processes,
                     SandeshLevel.SYS_DEBUG)
        for proc_info in all_processes:
            proc_name = self._get_process_name(proc_info)
            proc_pid = int(proc_info['pid'])

            stat = ProcessStat(proc_name, host_ip=self.hostip)
            stat.process_state = proc_info['statename']
            if 'start' in proc_info:
                stat.start_time = str(proc_info['start'])
                stat.start_count += 1
            stat.pid = proc_pid
            if stat.group not in self.group_names:
                self.group_names.append(stat.group)
            if not stat.group in process_state_db:
                process_state_db[stat.group] = {}
            process_state_db[stat.group][proc_name] = stat
        return process_state_db

    # In case the processes in the Node can change, update current processes
    def _update_current_processes(self):
        process_state_db = self._get_current_processes()
        msg = ("DBG: update_current_processes: process_state_db='%s'"
               % process_state_db)
        self.msg_log(msg, SandeshLevel.SYS_DEBUG)
        old_process_set = set(key for group in self.process_state_db
                                  for key in self.process_state_db[group])
        new_process_set = set(key for group in process_state_db
                                  for key in process_state_db[group])
        common_process_set = new_process_set.intersection(old_process_set)
        added_process_set = new_process_set - common_process_set
        deleted_process_set = old_process_set - common_process_set
        for deleted_process in deleted_process_set:
            self._delete_process_handler(deleted_process)
        for added_process in added_process_set:
            for group in process_state_db:
                if added_process in process_state_db[group]:
                    self._add_process_handler(
                        added_process, process_state_db[group][added_process])

    # process is deleted, send state & remove it from db
    def _delete_process_handler(self, deleted_process):
        for group in self.process_state_db:
            if deleted_process in self.process_state_db[group]:
                self.process_state_db[group][deleted_process].deleted = True
                self.send_process_state_db([group])
                del self.process_state_db[group][deleted_process]
                if not self.process_state_db[group]:
                    del self.process_state_db[group]
                return

    # new process added, update db & send state
    def _add_process_handler(self, added_process, process_info):
        group_val = process_info.group
        self.process_state_db[group_val][added_process] = process_info
        self.send_process_state_db([group_val])

    def _get_process_state_cb(self):
        state, description = self._get_process_state(self.fail_status_bits)
        return state, description

    def _get_build_info(self):
        # Retrieve build_info from package/rpm and cache it
        if self.curr_build_info is not None:
            return self.curr_build_info

        pkg_version = self._get_package_version()
        pkg_version_parts = pkg_version.split('-')
        build_id = pkg_version_parts[0]
        build_number = pkg_version_parts[1] if len(pkg_version_parts) > 1 else "unknown"
        self.new_build_info = build_info + '"build-id" : "' + \
            build_id + '", "build-number" : "' + \
            build_number + '"}]}'
        if (self.new_build_info != self.curr_build_info):
            self.curr_build_info = self.new_build_info
        return self.curr_build_info

    def _update_process_core_file_list(self):
        ret_value = False
        corenames = self.system_data.get_corefiles()
        process_state_db_tmp = copy.deepcopy(self.process_state_db)

        for group in process_state_db_tmp:
            for key in process_state_db_tmp[group]:
                process_state_db_tmp[group][key].core_file_list = []

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
                while len(process_state_db_tmp[group][key].core_file_list) > self.max_cores:
                    try:
                        if os.path.isfile(process_state_db_tmp[group][key].core_file_list[0]):
                            self.system_data.remove_corefiles([process_state_db_tmp[group][key].core_file_list[0]])
                    except OSError as e:
                        sys.stderr.write('ERROR: ' + str(e) + '\n')
                    del process_state_db_tmp[group][key].core_file_list[0]

        for group in self.process_state_db:
            for key in self.process_state_db[group]:
                if set(process_state_db_tmp[group][key].core_file_list) != set(self.process_state_db[group][key].core_file_list):
                    self.process_state_db[group][key].core_file_list = process_state_db_tmp[group][key].core_file_list
                    ret_value = True

        return ret_value

    def send_process_state_db(self, group_names):
        name = self.hostname
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
                # in tor-agent case, we should use tor-agent name as uve key
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
            node_status.build_info = self._get_build_info()
            node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                            data=node_status)
            msg = ('send_process_state_db: Sending UVE: {}'.format(node_status_uve))
            self.msg_log(msg, SandeshLevel.SYS_INFO)
            node_status_uve.send()

    def _send_process_state(self, process_info):
        pname = self._get_process_name(process_info)
        # update process stats
        if pname in list(key for group in self.process_state_db for key in self.process_state_db[group]):
            for group in self.process_state_db:
                if pname in self.process_state_db[group]:
                    proc_stat = self.process_state_db[group][pname]
        else:
            proc_stat = ProcessStat(pname, host_ip=self.hostip)

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
        send_init_uve = False
        # update process state database
        if not proc_stat.group in self.process_state_db:
            self.process_state_db[proc_stat.group] = {}
            send_init_uve = True
        self.process_state_db[proc_stat.group][pname] = proc_stat
        if send_uve:
            if (send_init_uve):
                self._send_init_info(proc_stat.group)
            self.send_process_state_db([proc_stat.group])

    def send_nodemgr_process_status(self):
        if self.prev_fail_status_bits == self.fail_status_bits:
            return

        self.prev_fail_status_bits = self.fail_status_bits
        fail_status_bits = self.fail_status_bits
        state, description = self._get_process_state(fail_status_bits)
        conn_infos = ConnectionState._connection_map.values()
        (cb_state, cb_description) = ConnectionState.get_conn_state_cb(conn_infos)
        if (cb_state == ProcessState.NON_FUNCTIONAL):
            state = ProcessState.NON_FUNCTIONAL
        if description != '':
            description += ' '
        description += cb_description

        process_status = ProcessStatus(
            module_id=self.type_info._module_name, instance_id=self.instance_id,
            state=ProcessStateNames[state], description=description,
            connection_infos=conn_infos)
        process_status_list = []
        process_status_list.append(process_status)
        node_status = NodeStatus(name=self.hostname,
                        process_status=process_status_list)
        node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                        data=node_status)
        msg = ('send_nodemgr_process_status: Sending UVE:'
               + str(node_status_uve))
        self.msg_log(msg, SandeshLevel.SYS_INFO)
        node_status_uve.send()

    def _get_package_version(self):
        pkg_version = utils.get_package_version('contrail-nodemgr')
        if pkg_version is None:
            self.msg_log('Error getting %s package version' % (
                'contrail-nodemgr'), SandeshLevel.SYS_ERR)
            pkg_version = "unknown"
        return pkg_version

    def _send_init_info(self, group_name):
        key = next(key for key in self.process_state_db[group_name])
        # system_cpu_info
        sys_cpu = SystemCpuInfo()
        sys_cpu.num_socket = self.system_data.get_num_socket()
        sys_cpu.num_cpu = self.system_data.get_num_cpu()
        sys_cpu.num_core_per_socket = self.system_data.get_num_core_per_socket()
        sys_cpu.num_thread_per_core = self.system_data.get_num_thread_per_core()

        node_status = NodeStatus(
            name=self.process_state_db[group_name][key].name,
            system_cpu_info=sys_cpu,
            build_info=self._get_build_info())

        # installed/running package version
        pkg_version = self._get_package_version()
        node_status.installed_package_version = pkg_version
        node_status.running_package_version = pkg_version

        node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                        data=node_status)
        node_status_uve.send()

    def _get_group_processes_mem_cpu_usage(self, group_name):
        process_mem_cpu_usage = {}
        for key in self.process_state_db[group_name]:
            pstat = self.process_state_db[group_name][key]
            if pstat.process_state != 'PROCESS_STATE_RUNNING':
                continue
            mem_cpu_usage_data = (
                self.process_info_manager.get_mem_cpu_usage_data(
                    pstat.pid, pstat.last_cpu, pstat.last_time))
            process_mem_cpu = mem_cpu_usage_data.get_process_mem_cpu_info()
            process_mem_cpu.__key = pstat.pname
            process_mem_cpu_usage[process_mem_cpu.__key] = process_mem_cpu
            pstat.last_cpu = mem_cpu_usage_data.last_cpu
            pstat.last_time = mem_cpu_usage_data.last_time

        return process_mem_cpu_usage

    def _get_process_state(self, fail_status_bits):
        if not fail_status_bits:
            return ProcessState.FUNCTIONAL, ''

        state = ProcessState.NON_FUNCTIONAL
        description = self.get_failbits_nodespecific_desc(fail_status_bits)
        if fail_status_bits & self.FAIL_STATUS_NTP_SYNC:
            description.append("NTP state unsynchronized.")
        return state, " ".join(description)

    # method to implement in children
    def get_failbits_nodespecific_desc(self, fail_status_bits):
        return list()

    def _event_process_state(self, process_info):
        msg = ("DBG: event_process_state:" + process_info['name'] + ","
               + "group:" + process_info['group'] + "," + "state:"
               + process_info['state'])
        self.msg_log(msg, SandeshLevel.SYS_DEBUG)
        self._send_process_state(process_info)

    def _event_process_communication(self, pdata):
        flag_and_value = pdata.partition(":")
        msg = ("DBG: event_process_communication: Flag:" + flag_and_value[0] +
               " Value:" + flag_and_value[2])
        self.msg_log(msg, SandeshLevel.SYS_DEBUG)

    def _event_tick_60(self):
        self.tick_count += 1
        for group in self.process_state_db:
            key = next(key for key in self.process_state_db[group])
            # get disk usage info periodically
            disk_usage_info = self.system_data.get_disk_usage()

            # typical ntp sync time is about 5 min - first time,
            # we scan only after 10 min
            if self.tick_count >= 10:
                if self.system_data.check_ntp_status():
                    self.fail_status_bits &= ~self.FAIL_STATUS_NTP_SYNC
                else:
                    self.fail_status_bits |= self.FAIL_STATUS_NTP_SYNC
                self.send_nodemgr_process_status()
            if self._update_process_core_file_list():
                self.send_process_state_db([group])

            process_mem_cpu_usage = self._get_group_processes_mem_cpu_usage(group)

            # get system mem/cpu usage
            system_mem_usage = self.system_data.get_sys_mem_info(
                self.type_info._uve_node_type)
            system_cpu_usage = self.system_data.get_sys_cpu_info(
                self.type_info._uve_node_type)

            # send above encoded buffer
            node_status = NodeStatus(
                name=self.process_state_db[group][key].name,
                disk_usage_info=disk_usage_info,
                system_mem_usage=system_mem_usage,
                system_cpu_usage=system_cpu_usage,
                process_mem_cpu_usage=process_mem_cpu_usage)
            # encode other core file
            if self.system_data.update_all_core_file():
                self.send_process_state_db(self.group_names)
                node_status.all_core_file_list = self.all_core_file_list

            node_status_uve = NodeStatusUVE(table=self.type_info._object_table,
                                            data=node_status)

            self.msg_log('DBG: event_tick_60: node_status=%s' % node_status,
                         SandeshLevel.SYS_DEBUG)
            node_status_uve.send()

    def send_init_data(self):
        self.send_nodemgr_process_status()
        self.send_process_state_db(self.group_names)

    # can be overriden in children
    def do_periodic_events(self):
        self._event_tick_60()

    def run_periodically(self, function, interval, *args, **kwargs):
        while True:
            before = time.time()
            function(*args, **kwargs)

            duration = time.time() - before
            if duration < interval:
                gevent.sleep(interval - duration)
            else:
                self.msg_log(
                    'function %s duration exceeded %f interval (took %f)'
                        % (function.__name__, interval, duration),
                    SandeshLevel.SYS_ERR)

    def runforever(self):
        while True:
            try:
                self.process_info_manager.run_job()
            except Exception as e:
                self.msg_log('Exception in periodic job: {}'.format(e), SandeshLevel.SYS_WARN)
            gevent.sleep(seconds=5)

    def nodemgr_sighup_handler(self):
        collector_list = list()
        config = ConfigParser.SafeConfigParser()
        config.read([self.config.config_file_path])
        if 'COLLECTOR' in config.sections():
            try:
                collector = config.get('COLLECTOR', 'server_list')
                collector_list = collector.split()
            except ConfigParser.NoOptionError:
                pass
        collector_list.sort()
        new_chksum = hashlib.md5("".join(collector_list)).hexdigest()
        if new_chksum != self.collector_chksum:
            self.collector_chksum = new_chksum
            self.random_collectors = random.sample(collector_list, len(collector_list))
            self.sandesh_instance.reconfig_collectors(self.random_collectors)
