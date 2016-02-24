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
import socket
import time
import subprocess
from subprocess import Popen, PIPE
import supervisor.xmlrpc
import xmlrpclib

from supervisor import childutils
from nodemgr.common.event_listener_protocol_nodemgr import \
    EventListenerProtocolNodeMgr
from nodemgr.common.process_stat import ProcessStat
from sandesh_common.vns.constants import INSTANCE_ID_DEFAULT
import discoveryclient.client as client
from buildinfo import build_info
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


class EventManager(object):
    rules_data = []
    group_names = []
    process_state_db = {}
    FAIL_STATUS_DUMMY = 0x1
    FAIL_STATUS_DISK_SPACE = 0x2
    FAIL_STATUS_SERVER_PORT = 0x4
    FAIL_STATUS_NTP_SYNC = 0x8
    FAIL_STATUS_DISK_SPACE_NA = 0x10

    def __init__(self, rule_file, discovery_server,
                 discovery_port, collector_addr, sandesh_global,
                 send_build_info = False):
        self.stdin = sys.stdin
        self.stdout = sys.stdout
        self.stderr = sys.stderr
        self.rule_file = rule_file
        self.rules_data = ''
        self.max_cores = 4
        self.max_old_cores = 3
        self.max_new_cores = 1
        self.all_core_file_list = []
        self.core_dir_modified_time = 0
        self.tick_count = 0
        self.fail_status_bits = 0
        self.prev_fail_status_bits = 1
        self.instance_id = INSTANCE_ID_DEFAULT
        self.discovery_server = discovery_server
        self.discovery_port = discovery_port
        self.collector_addr = collector_addr
        self.listener_nodemgr = EventListenerProtocolNodeMgr()
        self.sandesh_global = sandesh_global
        self.curr_build_info = None
        self.new_build_info = None
        self.send_build_info = send_build_info

    # Get all the current processes in the node
    def get_current_process(self):
        proxy = xmlrpclib.ServerProxy(
            'http://127.0.0.1',
            transport=supervisor.xmlrpc.SupervisorTransport(
                None, None, serverurl=self.supervisor_serverurl))
        # Add all current processes to make sure nothing misses the radar
        process_state_db = {}
        for proc_info in proxy.supervisor.getAllProcessInfo():
            if (proc_info['name'] != proc_info['group']):
                proc_name = proc_info['group'] + ":" + proc_info['name']
            else:
                proc_name = proc_info['name']
            process_stat_ent = self.get_process_stat_object(proc_name)
            process_stat_ent.process_state = "PROCESS_STATE_" + \
                proc_info['statename']
            if (process_stat_ent.process_state ==
                    'PROCESS_STATE_RUNNING'):
                process_stat_ent.start_time = str(proc_info['start'] * 1000000)
                process_stat_ent.start_count += 1
            process_state_db[proc_name] = process_stat_ent
        return process_state_db
    # end get_current_process

    # Add the current processes in the node to db
    def add_current_process(self):
        self.process_state_db = self.get_current_process()
    # end add_current_process

    # In case the processes in the Node can change, update current processes
    def update_current_process(self):
        process_state_db = self.get_current_process()
        old_process_set = set(self.process_state_db.keys())
        new_process_set = set(process_state_db.keys())
        common_process_set = new_process_set.intersection(old_process_set)
        added_process_set = new_process_set - common_process_set
        deleted_process_set = old_process_set - common_process_set
        for deleted_process in deleted_process_set:
            self.delete_process_handler(deleted_process)
        for added_process in added_process_set:
            self.add_process_handler(
                added_process, process_state_db[added_process])
    # end update_current_process

    # process is deleted, send state & remove it from db
    def delete_process_handler(self, deleted_process):
        self.process_state_db[deleted_process].deleted = True
        group_val = self.process_state_db[deleted_process].group
        self.send_process_state_db([group_val])
        del self.process_state_db[deleted_process]
    # end delete_process_handler

    # new process added, update db & send state
    def add_process_handler(self, added_process, process_info):
        self.process_state_db[added_process] = process_info
        group_val = self.process_state_db[added_process].group
        self.send_process_state_db([group_val])
    # end add_process_handler

    def get_discovery_client(self):
        _disc = client.DiscoveryClient(
            self.discovery_server, self.discovery_port, self.module_id)
        return _disc

    def check_ntp_status(self):
        ntp_status_cmd = 'ntpq -n -c pe | grep "^*"'
        proc = Popen(ntp_status_cmd, shell=True, stdout=PIPE, stderr=PIPE)
        (output, errout) = proc.communicate()
        if proc.returncode != 0:
            self.fail_status_bits |= self.FAIL_STATUS_NTP_SYNC
        else:
            self.fail_status_bits &= ~self.FAIL_STATUS_NTP_SYNC
        self.send_nodemgr_process_status()

    def _add_build_info(self, node_status):
        # Retrieve build_info from package/rpm and cache it
        if self.curr_build_info is None:
            command = "contrail-version contrail-nodemgr | grep contrail-nodemgr"
            version = os.popen(command).read()
            _, rpm_version, build_num = version.split()
            self.new_build_info = build_info + '"build-id" : "' + \
                rpm_version + '", "build-number" : "' + \
                build_num + '"}]}'
            if (self.new_build_info != self.curr_build_info):
                self.curr_build_info = self.new_build_info
                node_status.build_info = self.curr_build_info

    def send_process_state_db_base(self, group_names, ProcessInfo,
                                   NodeStatus, NodeStatusUVE):
        name = socket.gethostname()
        for group in group_names:
            process_infos = []
            delete_status = True
            for key in self.process_state_db:
                pstat = self.process_state_db[key]
                if (pstat.group != group):
                    continue
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
            node_status.all_core_file_list = self.all_core_file_list
            if (self.send_build_info):
                self._add_build_info(node_status)
            node_status_uve = NodeStatusUVE(data=node_status)
	    msg = 'Sending UVE:' + str(node_status_uve) 
            self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
			    SandeshLevel.SYS_INFO), msg)
            node_status_uve.send()

    def send_all_core_file(self):
        stat_command_option = "stat --printf=%Y /var/crashes"
        modified_time = Popen(
            stat_command_option.split(),
            stdout=PIPE).communicate()
        if modified_time[0] == self.core_dir_modified_time:
            return
        self.core_dir_modified_time = modified_time[0]
        ls_command_option = "ls /var/crashes"
        (corename, stderr) = Popen(
            ls_command_option.split(),
            stdout=PIPE).communicate()
        self.all_core_file_list = corename.split('\n')[0:-1]
        self.send_process_state_db(self.group_names)

    def get_process_stat_object(self, pname):
        return ProcessStat(pname)

    def send_process_state(self, pname, pstate, pheaders):
        # update process stats
        if pname in self.process_state_db.keys():
            proc_stat = self.process_state_db[pname]
        else:
            proc_stat = self.get_process_stat_object(pname)
            if not proc_stat.group in self.group_names:
                self.group_names.append(proc_stat.group)

        proc_stat.process_state = pstate

        send_uve = False
        if (pstate == 'PROCESS_STATE_RUNNING'):
            proc_stat.start_count += 1
            proc_stat.start_time = str(int(time.time() * 1000000))
            send_uve = True

        if (pstate == 'PROCESS_STATE_STOPPED'):
            proc_stat.stop_count += 1
            send_uve = True
            proc_stat.stop_time = str(int(time.time() * 1000000))
            proc_stat.last_exit_unexpected = False

        if (pstate == 'PROCESS_STATE_EXITED'):
            proc_stat.exit_count += 1
            send_uve = True
            proc_stat.exit_time = str(int(time.time() * 1000000))
            if not(int(pheaders['expected'])):
                self.stderr.write(
                    pname + " with pid:" + pheaders['pid'] +
                    " exited abnormally\n")
                proc_stat.last_exit_unexpected = True
                # check for core file for this exit
                find_command_option = \
                    "find /var/crashes -name core.[A-Za-z]*." + \
                    pheaders['pid'] + "*"
                self.stderr.write(
                    "find command option for cores:" +
                    find_command_option + "\n")
                (corename, stderr) = Popen(
                    find_command_option.split(),
                    stdout=PIPE).communicate()
                self.stderr.write("core file: " + corename + "\n")

                if ((corename is not None) and (len(corename.rstrip()) >= 1)):
                    # before adding to the core file list make
                    # sure that we do not have too many cores
                    sys.stderr.write(
                        'core_file_list:' + str(proc_stat.core_file_list) +
                        ", self.max_cores:" + str(self.max_cores) + "\n")
                    if (len(proc_stat.core_file_list) == self.max_cores):
                        # get rid of old cores
                        sys.stderr.write(
                            'max # of cores reached:' +
                            str(self.max_cores) + "\n")
                        val = self.max_cores - self.max_new_cores + 1
                        core_files_to_be_deleted = \
                            proc_stat.core_file_list[self.max_old_cores:(val)]
                        sys.stderr.write(
                            'deleting core file list:' +
                            str(core_files_to_be_deleted) + "\n")
                        for core_file in core_files_to_be_deleted:
                            sys.stderr.write(
                                'deleting core file:' + core_file + "\n")
                            try:
                                os.remove(core_file)
                            except OSError as e:
                                sys.stderr.write('ERROR: ' + str(e) + '\n')
                        # now delete the list as well
                        val = self.max_cores - self.max_new_cores + 1
                        del proc_stat.core_file_list[self.max_old_cores:(val)]
                    # now add the new core to the core file list
                    proc_stat.core_file_list.append(corename.rstrip())
                    sys.stderr.write(
                        "# of cores for " + pname + ":" +
                        str(len(proc_stat.core_file_list)) + "\n")

        # update process state database
        self.process_state_db[pname] = proc_stat
        f = open('/var/log/contrail/process_state' +
                 self.node_type + ".json", 'w')
        f.write(json.dumps(
            self.process_state_db,
            default=lambda obj: obj.__dict__))

        if not(send_uve):
            return

        if (send_uve):
            self.send_process_state_db([proc_stat.group])

    def send_nodemgr_process_status_base(self, ProcessStateNames,
                                         ProcessState, ProcessStatus,
                                         NodeStatus, NodeStatusUVE):
        if (self.prev_fail_status_bits != self.fail_status_bits):
            self.prev_fail_status_bits = self.fail_status_bits
            fail_status_bits = self.fail_status_bits
            state, description = self.get_process_state(fail_status_bits)
            process_status = ProcessStatus(
                    module_id=self.module_id, instance_id=self.instance_id,
                    state=state, description=description)
            process_status_list = []
            process_status_list.append(process_status)
            node_status = NodeStatus(name=socket.gethostname(),
                            process_status=process_status_list)
            if (self.send_build_info):
                self._add_build_info(node_status)
            node_status_uve = NodeStatusUVE(data=node_status)
            msg = 'Sending UVE:' + str(node_status_uve)
            self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
                                    SandeshLevel.SYS_INFO), msg)
            node_status_uve.send()

    def send_disk_usage_info_base(self, NodeStatusUVE, NodeStatus,
                                  DiskPartitionUsageStats):
        partition = subprocess.Popen(
            "df -T -t ext2 -t ext3 -t ext4 -t xfs",
            shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        disk_usage_infos = []
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
                disk_usage_stat.partition_name = str(partition_name)
                disk_usage_stat.partition_space_used_1k = \
                    int(partition_space_used_1k)
                disk_usage_stat.partition_space_available_1k = \
                    int(partition_space_available_1k)
            except ValueError:
                sys.stderr.write("Failed to get local disk space usage" + "\n")
            else:
                disk_usage_infos.append(disk_usage_stat)

        # send node UVE
        node_status = NodeStatus(
            name=socket.gethostname(), disk_usage_info=disk_usage_infos)
        if (self.send_build_info):
            self._add_build_info(node_status)
        node_status_uve = NodeStatusUVE(data=node_status)
	msg = 'Sending UVE:' + str(node_status_uve)
	self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
			    SandeshLevel.SYS_INFO), msg)
        node_status_uve.send()
    # end send_disk_usage_info

    def get_process_state_base(self, fail_status_bits,
                               ProcessStateNames, ProcessState):
        if fail_status_bits:
            state = ProcessStateNames[ProcessState.NON_FUNCTIONAL]
            description = self.get_failbits_nodespecific_desc(fail_status_bits)
            if (description is ""):
                if fail_status_bits & self.FAIL_STATUS_NTP_SYNC:
                    if description != "":
                        description += " "
                    description += "NTP state unsynchronized."
        else:
            state = ProcessStateNames[ProcessState.FUNCTIONAL]
            description = ''
        return state, description

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        return ""

    def event_process_state(self, pheaders, headers):
	msg = ("process:" + pheaders['processname'] + "," + "groupname:" + 
		pheaders['groupname'] + "," + "eventname:" + headers['eventname'])
	self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(SandeshLevel.SYS_DEBUG), msg)
        pname = pheaders['processname']
        if (pheaders['processname'] != pheaders['groupname']):
            pname = pheaders['groupname'] + ":" + pheaders['processname']
        self.send_process_state(pname, headers['eventname'], pheaders)
        for rules in self.rules_data['Rules']:
            if 'processname' in rules:
                if ((rules['processname'] == pheaders['groupname']) and
                   (rules['process_state'] == headers['eventname'])):
		    msg = "got a hit with:" + str(rules)
		    self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
			    SandeshLevel.SYS_DEBUG), msg)
                    # do not make async calls
                    try:
                        ret_code = subprocess.call(
                            [rules['action']], shell=True,
                            stdout=self.stderr, stderr=self.stderr)
                    except Exception as e:
		        msg = ('Failed to execute action: ' + rules['action'] +
				 ' with err ' + str(e))
			self.sandesh_global.logger().logger.log(SandeshLogger.
                                get_py_logger_level(SandeshLevel.SYS_ERR), msg)
                    else:
                        if ret_code:
			    msg = ('Execution of action ' + rules['action'] + 
					' returned err ' + str(ret_code))
			    self.sandesh_global.logger().log(SandeshLogger.
                                    get_py_logger_level(SandeshLevel.SYS_ERR), msg)

    def event_process_communication(self, pdata):
        flag_and_value = pdata.partition(":")
        msg = ("Flag:" + flag_and_value[0] +
                " Value:" + flag_and_value[2])
        self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level
                (SandeshLevel.SYS_DEBUG), msg)
        for rules in self.rules_data['Rules']:
            if 'flag_name' in rules:
                if ((rules['flag_name'] == flag_and_value[0]) and
                   (rules['flag_value'].strip() == flag_and_value[2].strip())):
                    msg = "got a hit with:" + str(rules)
                    self.sandesh_global.logger().log(SandeshLogger.
                            get_py_logger_level(SandeshLevel.SYS_DEBUG), msg)
                    cmd_and_args = ['/usr/bin/bash', '-c', rules['action']]
                    subprocess.Popen(cmd_and_args)

    def event_tick_60(self, prev_current_time):
        self.tick_count += 1
        # send other core file
        self.send_all_core_file()
        # send disk usage info periodically
        self.send_disk_usage_info()
        # typical ntp sync time is about 5 min - first time,
        # we scan only after 10 min
        if self.tick_count >= 10:
            self.check_ntp_status()

        current_time = int(time.time())
        if ((abs(current_time - prev_current_time)) > 300):
            # update all process start_times with the updated time
            # Compute the elapsed time and subtract them from
            # current time to get updated values
            sys.stderr.write(
                "Time lapse detected " +
                str(abs(current_time - prev_current_time)) + "\n")
            for key in self.process_state_db:
                pstat = self.process_state_db[key]
                if pstat.start_time is not '':
                    pstat.start_time = str(
                        (int(current_time - (prev_current_time -
                             ((int)(pstat.start_time)) / 1000000))) * 1000000)
                if (pstat.process_state == 'PROCESS_STATE_STOPPED'):
                    if pstat.stop_time is not '':
                        pstat.stop_time = str(
                            int(current_time - (prev_current_time -
                                ((int)(pstat.stop_time)) / 1000000)) *
                            1000000)
                if (pstat.process_state == 'PROCESS_STATE_EXITED'):
                    if pstat.exit_time is not '':
                        pstat.exit_time = str(
                            int(current_time - (prev_current_time -
                                ((int)(pstat.exit_time)) / 1000000)) *
                            1000000)
                # update process state database
                self.process_state_db[key] = pstat
            try:
                json_file = '/var/log/contrail/process_state' + \
                    self.node_type + ".json"
                f = open(json_file, 'w')
                f.write(
                    json.dumps(
                        self.process_state_db,
                        default=lambda obj: obj.__dict__))
            except:
                sys.stderr.write("Unable to write json")
                pass
            self.send_process_state_db(self.group_names)
        prev_current_time = int(time.time())
        return prev_current_time

    def runforever(self, test=False):
        prev_current_time = int(time.time())
        while 1:
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self.listener_nodemgr.wait(
                self.stdin, self.stdout)
            pheaders, pdata = childutils.eventdata(payload + '\n')

            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                self.event_process_state(pheaders, headers)
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self.event_process_communication(pdata)
            # do periodic events
            if headers['eventname'].startswith("TICK_60"):
                prev_current_time = self.event_tick_60(prev_current_time)
            self.listener_nodemgr.ok(self.stdout)
