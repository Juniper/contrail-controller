doc = " "

from gevent import monkey; monkey.patch_all()
import os
import sys
import socket
import subprocess
import json
import time
import datetime
import platform
import select
import gevent
import ConfigParser

from nodemgr.EventManager import EventManager
from nodemgr.process_stat import process_stat

from ConfigParser import NoOptionError

from supervisor import childutils

from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType, INSTANCE_ID_DEFAULT
from subprocess import Popen, PIPE
from StringIO import StringIO

from database.sandesh.database.ttypes import \
    NodeStatusUVE, NodeStatus
from database.sandesh.database.process_info.ttypes import \
    ProcessStatus, ProcessState, ProcessInfo
from database.sandesh.database.process_info.constants import \
    ProcessStateNames

def usage():
    print doc
    sys.exit(255)

class DatabaseEventManager(EventManager):
    def __init__(self, rule_file, discovery_server, discovery_port, collector_addr):
        EventManager.__init__(self, rule_file, discovery_server, discovery_port, collector_addr)
        self.node_type = "contrail-database"
        self.module = Module.DATABASE_NODE_MGR
        self.module_id =  ModuleNames[self.module]
        self.supervisor_serverurl = "unix:///tmp/supervisord_database.sock"
        self.add_current_process(process_stat)
    #end __init__

    def process(self):
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/supervisord_database_files/contrail-database.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        config_file = '/etc/contrail/contrail-database-nodemgr.conf'
        Config = self.read_config_data(config_file)
        self.get_collector_list(Config)
        _disc = self.get_discovery_client(Config)
        sandesh_global.init_generator(self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, [],
            self.module_id, 8103, ['database.sandesh'],_disc)
        #sandesh_global.set_logging_params(enable_local_log=True)
        self.sandesh_global = sandesh_global

        try:
            self.hostip = Config.get("DEFAULT", "hostip")
        except:
            self.hostip = '127.0.0.1'

        (linux_dist, x, y) = platform.linux_distribution()
        if (linux_dist == 'Ubuntu'):
            (disk_space_used, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/cassandra.yaml | grep '-' | cut -d'-' -f2 \`/ContrailAnalytics | grep %` && echo $3 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (disk_space_available, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics | grep %` && echo $4  | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (analytics_db_size, error_value) = Popen("set `du -skL \`grep -A 1 'data_file_directories:'  /etc/cassandra/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics` && echo $1 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
        else:
            (disk_space_used, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/conf/cassandra.yaml | grep '-' | cut -d'-' -f2 \`/ContrailAnalytics | grep %` && echo $3 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (disk_space_available, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/conf/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics | grep %` && echo $4  | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (analytics_db_size, error_value) = Popen("set `du -skL \`grep -A 1 'data_file_directories:'  /etc/cassandra/conf/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics` && echo $1 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
        disk_space_total = int(disk_space_used) + int(disk_space_available)
        try:
            min_disk_opt = Config.get("DEFAULT", "minimum_diskGB")
            min_disk = int(min_disk_opt)
        except:
            min_disk = 0
        if (disk_space_total/(1024*1024) < min_disk):
            from sandesh_common.vns.constants import SERVICE_CONTRAIL_DATABASE

            cmd_str = "service " + SERVICE_CONTRAIL_DATABASE + " stop"
            (ret_value, error_value) = Popen(cmd_str, shell=True, stdout=PIPE).communicate()
            prog.fail_status_bits |= prog.FAIL_STATUS_DISK_SPACE

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(group_names, ProcessInfo, NodeStatus, NodeStatusUVE)

    def send_nodemgr_process_status(self):
        self.send_nodemgr_process_status_base(ProcessStateNames, ProcessState, ProcessStatus, NodeStatus, NodeStatusUVE)

    def get_process_state(self, fail_status_bits):
        if fail_status_bits:
            state = ProcessStateNames[ProcessState.NON_FUNCTIONAL]
            description = ""
            if fail_status_bits & self.FAIL_STATUS_DISK_SPACE:
                description += "Disk for analytics db is too low, cassandra stopped."
            if fail_status_bits & self.FAIL_STATUS_SERVER_PORT:
                if description != "":
                    description += " "
                description += "Cassandra state detected DOWN."
            if fail_status_bits & self.FAIL_STATUS_NTP_SYNC:
                if description != "":
                    description += " "
                description += "NTP state unsynchronized."
        else:
            state = ProcessStateNames[ProcessState.FUNCTIONAL]
            description = ''
        return state, description

    def database_periodic(self):
        from database.sandesh.database.ttypes import \
            DatabaseUsageStats, DatabaseUsageInfo, DatabaseUsage

        (linux_dist, x, y) = platform.linux_distribution()
        if (linux_dist == 'Ubuntu'):
            (disk_space_used, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/cassandra.yaml | grep '-' | cut -d'-' -f2 \`/ContrailAnalytics | grep %` && echo $3 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (disk_space_available, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics | grep %` && echo $4  | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (analytics_db_size, error_value) = Popen("set `du -skL \`grep -A 1 'data_file_directories:'  /etc/cassandra/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics` && echo $1 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
        else:
            (disk_space_used, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/conf/cassandra.yaml | grep '-' | cut -d'-' -f2 \`/ContrailAnalytics | grep %` && echo $3 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (disk_space_available, error_value) = Popen("set `df -Pk \`grep -A 1 'data_file_directories:'  /etc/cassandra/conf/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics | grep %` && echo $4  | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
            (analytics_db_size, error_value) = Popen("set `du -skL \`grep -A 1 'data_file_directories:'  /etc/cassandra/conf/cassandra.yaml | grep '-' | cut -d'-' -f2\`/ContrailAnalytics` && echo $1 | cut -d'%' -f1", shell=True, stdout=PIPE).communicate()
        db_stat = DatabaseUsageStats()
        db_info = DatabaseUsageInfo()
        try:
            db_stat.disk_space_used_1k = int(disk_space_used)
            db_stat.disk_space_available_1k = int(disk_space_available)
            db_stat.analytics_db_size_1k = int(analytics_db_size)
        except ValueError:
            sys.stderr.write("Failed to get database usage" + "\n")
        else:
            db_info.name = socket.gethostname()
            db_info.database_usage = db_stat
            db_info.database_usage_stats = [db_stat]
            usage_stat = DatabaseUsage(data=db_info)
            usage_stat.send()

        cassandra_cli_cmd = "cassandra-cli --host " + self.hostip + " --batch  < /dev/null | grep 'Connected to:'"
        proc = Popen(cassandra_cli_cmd, shell=True, stdout=PIPE, stderr=PIPE)
        (output, errout) = proc.communicate()
        if proc.returncode != 0:
            self.fail_status_bits |= self.FAIL_STATUS_SERVER_PORT
        else:
            self.fail_status_bits &= ~self.FAIL_STATUS_SERVER_PORT
        self.send_nodemgr_process_status()

    # end database_periodic

    def runforever(self, test=False):
        prev_current_time = int(time.time())
        while 1:
            gevent.sleep(1)
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self.listener_nodemgr.wait(self.stdin, self.stdout)

            #self.stderr.write("headers:\n" + str(headers) + '\n')
            #self.stderr.write("payload:\n" + str(payload) + '\n')

            pheaders, pdata = childutils.eventdata(payload+'\n')
            #self.stderr.write("pheaders:\n" + str(pheaders)+'\n')
            #self.stderr.write("pdata:\n" + str(pdata))
            
            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                self.event_process_state(pheaders, headers)
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self.event_process_communication(pdata)
            # do periodic events
            if headers['eventname'].startswith("TICK_60"):
                self.database_periodic()
                prev_current_time = self.event_tick_60(prev_current_time)
            self.listener_nodemgr.ok(self.stdout)
