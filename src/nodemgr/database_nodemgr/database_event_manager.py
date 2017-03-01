#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from gevent import monkey
monkey.patch_all()

import os
import subprocess
import socket
import platform
import yaml
from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.database_nodemgr.common import CassandraManager
from pysandesh.sandesh_base import sandesh_global
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_logger import SandeshLogger
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ThreadPoolNames
from database.sandesh.database.ttypes import \
    DatabaseUsageStats, DatabaseUsageInfo, DatabaseUsage, CassandraStatusUVE,\
    CassandraStatusData,CassandraThreadPoolStats, CassandraCompactionTask

class DatabaseEventManager(EventManager):
    def __init__(self, rule_file, unit_names, collector_addr, sandesh_config,
                 hostip, minimum_diskgb, contrail_databases,
                 cassandra_repair_interval,
                 cassandra_repair_logdir):

        if os.path.exists('/tmp/supervisord_database.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_database.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_database.sock"
        type_info = EventManagerTypeInfo(
            package_name = 'contrail-database-common',
            object_table = "ObjectDatabaseInfo",
            module_type = Module.DATABASE_NODE_MGR,
            supervisor_serverurl = supervisor_serverurl,
            third_party_processes =  {
                "cassandra" : "Dcassandra-pidfile=.*cassandra\.pid",
                "zookeeper" : "org.apache.zookeeper.server.quorum.QuorumPeerMain"
            },
            sandesh_packages = ['database.sandesh'],
            unit_names = unit_names)
        EventManager.__init__(
            self, type_info, rule_file,
            collector_addr, sandesh_global, sandesh_config)
        self.hostip = hostip
        self.minimum_diskgb = minimum_diskgb
        self.contrail_databases = contrail_databases
        self.cassandra_repair_interval = cassandra_repair_interval
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self.cassandra_mgr = CassandraManager(cassandra_repair_logdir)
        # Initialize tpstat structures
        self.cassandra_status_old = CassandraStatusData()
        self.cassandra_status_old.cassandra_compaction_task = CassandraCompactionTask()
        self.cassandra_status_old.thread_pool_stats = []
    # end __init__

    def _get_cassandra_config_option(self, config):
        (linux_dist, x, y) = platform.linux_distribution()
        if (linux_dist == 'Ubuntu'):
            yamlstream = open("/etc/cassandra/cassandra.yaml", 'r')
        else:
            yamlstream = open("/etc/cassandra/conf/cassandra.yaml", 'r')

        cfg = yaml.safe_load(yamlstream)
        yamlstream.close()
        return cfg[config]

    @staticmethod
    def cassandra_old():
        (PLATFORM, VERSION, EXTRA) = platform.linux_distribution()
        if PLATFORM.lower() == 'ubuntu':
            if VERSION.find('12.') == 0:
                return True
        if PLATFORM.lower() == 'centos':
            if VERSION.find('6.') == 0:
                return True
        return False

    def process(self):
        self.load_rules_data()
        try:
            cassandra_data_dirs = self._get_cassandra_config_option("data_file_directories")
            cassandra_data_dir_exists = False
            total_disk_space_used = 0
            total_disk_space_available = 0
            for cassandra_data_dir in cassandra_data_dirs:
                if DatabaseEventManager.cassandra_old():
                    analytics_dir = cassandra_data_dir + '/ContrailAnalytics'
                else:
                    import glob
                    all_analytics_dirs = glob.glob(cassandra_data_dir + '/ContrailAnalyticsCql*')
                    if all_analytics_dirs:
                        #for now we assume the partition for all analytics clusters is same
                        analytics_dir = all_analytics_dirs[0]

                if os.path.exists(analytics_dir):
                    cassandra_data_dir_exists = True
                    msg = "analytics_dir is " + analytics_dir
                    self.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
                    df = subprocess.Popen(["df", analytics_dir],
                            stdout=subprocess.PIPE, close_fds=True)
                    output = df.communicate()[0]
                    device, size, disk_space_used, disk_space_available, \
                       percent, mountpoint = output.split("\n")[1].split()
                    total_disk_space_used += int(disk_space_used)
                    total_disk_space_available += int(disk_space_available)
            if cassandra_data_dir_exists == False:
                if 'analytics' not in self.contrail_databases:
                    self.fail_status_bits &= ~self.FAIL_STATUS_DISK_SPACE_NA
                else:
                    self.fail_status_bits |= self.FAIL_STATUS_DISK_SPACE_NA
            else:
                disk_space_analytics = int(total_disk_space_used) + int(total_disk_space_available)
                if (disk_space_analytics / (1024 * 1024) < self.minimum_diskgb):
                    cmd_str = "service " + SERVICE_CONTRAIL_DATABASE + " stop"
                    (ret_value, error_value) = subprocess.Popen(
                        cmd_str, shell=True, stdout=subprocess.PIPE,
                        close_fds=True).communicate()
                    self.fail_status_bits |= self.FAIL_STATUS_DISK_SPACE
                self.fail_status_bits &= ~self.FAIL_STATUS_DISK_SPACE_NA
        except:
            msg = "Failed to get database usage"
            self.msg_log(msg, level=SandeshLevel.SYS_ERR)
            self.fail_status_bits |= self.FAIL_STATUS_DISK_SPACE_NA

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        description = ""
        if fail_status_bits & self.FAIL_STATUS_DISK_SPACE:
            description += "Disk for analytics db is too low," + \
                " cassandra stopped."
        if fail_status_bits & self.FAIL_STATUS_SERVER_PORT:
            if description != "":
                description += " "
            description += "Cassandra state detected DOWN."
        if fail_status_bits & self.FAIL_STATUS_DISK_SPACE_NA:
            description += "Disk space for analytics db not retrievable."
        return description

    def database_periodic(self):
        try:
            cassandra_data_dirs = self._get_cassandra_config_option("data_file_directories")
            cassandra_data_dir_exists = False
            total_disk_space_used = 0
            total_disk_space_available = 0
            total_analytics_db_size = 0
            for cassandra_data_dir in cassandra_data_dirs:
                if DatabaseEventManager.cassandra_old():
                    analytics_dir = cassandra_data_dir + '/ContrailAnalytics'
                else:
                    import glob
                    all_analytics_dirs = glob.glob(cassandra_data_dir + '/ContrailAnalyticsCql*')
                    if all_analytics_dirs:
                        #for now we assume the partition for all analytics clusters is same
                        analytics_dir = all_analytics_dirs[0]

                if os.path.exists(analytics_dir):
                    cassandra_data_dir_exists = True
                    msg = "analytics_dir is " + analytics_dir
                    self.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
                    df = subprocess.Popen(["df", analytics_dir],
                            stdout=subprocess.PIPE, close_fds=True)
                    output = df.communicate()[0]
                    device, size, disk_space_used, disk_space_available, \
                        percent, mountpoint =  output.split("\n")[1].split()
                    total_disk_space_used += int(disk_space_used)
                    total_disk_space_available += int(disk_space_available)
                    du = subprocess.Popen(["du", "-skl", analytics_dir],
                            stdout=subprocess.PIPE, close_fds=True)
                    analytics_db_size, directory = du.communicate()[0].split()
                    total_analytics_db_size += int(analytics_db_size)
            if cassandra_data_dir_exists == False:
                if 'analytics' not in self.contrail_databases:
                    self.fail_status_bits &= ~self.FAIL_STATUS_DISK_SPACE_NA
                else:
                    self.fail_status_bits |= self.FAIL_STATUS_DISK_SPACE_NA
            else:
                self.fail_status_bits &= ~self.FAIL_STATUS_DISK_SPACE_NA

                db_stat = DatabaseUsageStats()
                db_info = DatabaseUsageInfo()

                db_stat.disk_space_used_1k = int(total_disk_space_used)
                db_stat.disk_space_available_1k = int(total_disk_space_available)
                db_stat.analytics_db_size_1k = int(total_analytics_db_size)

                db_info.name = socket.gethostname()
                db_info.database_usage = [db_stat]
                usage_stat = DatabaseUsage(data=db_info)
                usage_stat.send()
        except:
            msg = "Failed to get database usage"
            self.msg_log(msg, level=SandeshLevel.SYS_ERR)
            self.fail_status_bits |= self.FAIL_STATUS_DISK_SPACE_NA

        cqlsh_cmd = "cqlsh " + self.hostip + " -e quit"
        proc = subprocess.Popen(cqlsh_cmd, shell=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, close_fds=True)
        (output, errout) = proc.communicate()
        if proc.returncode != 0:
            self.fail_status_bits |= self.FAIL_STATUS_SERVER_PORT
        else:
            self.fail_status_bits &= ~self.FAIL_STATUS_SERVER_PORT
        self.send_nodemgr_process_status()
        # Send cassandra nodetool information
        self.send_database_status()
        # Record cluster status and shut down cassandra if needed
        self.cassandra_mgr.status()
    # end database_periodic

    def send_database_status(self):
        cassandra_status_uve = CassandraStatusUVE()
        cassandra_status = CassandraStatusData()
        cassandra_status.cassandra_compaction_task = CassandraCompactionTask()
        # Get compactionstats
        compaction_count = subprocess.Popen("nodetool compactionstats|grep 'pending tasks:'",
            shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            close_fds=True)
        op, err = compaction_count.communicate()
        if compaction_count.returncode != 0:
            msg = "Failed to get nodetool compactionstats " + err
            self.msg_log(msg, level=SandeshLevel.SYS_ERR)
            return
        cassandra_status.cassandra_compaction_task.pending_compaction_tasks = \
            self.get_pending_compaction_count(op)
        # Get the tpstats value
        tpstats_op = subprocess.Popen(["nodetool", "tpstats"], stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, close_fds=True)
        op, err = tpstats_op.communicate()
        if tpstats_op.returncode != 0:
            msg = "Failed to get nodetool tpstats " + err
            self.msg_log(msg, level=SandeshLevel.SYS_ERR)
            return
        cassandra_status.thread_pool_stats = self.get_tp_status(op)
        cassandra_status.name = socket.gethostname()
        cassandra_status_uve = CassandraStatusUVE(data=cassandra_status)
        if self.has_cassandra_status_changed(cassandra_status, self.cassandra_status_old):
            # Assign cassandra_status to cassandra_status_old
            self.cassandra_status_old.thread_pool_stats = \
                cassandra_status.thread_pool_stats
            self.cassandra_status_old.cassandra_compaction_task.\
                pending_compaction_tasks = cassandra_status.\
                cassandra_compaction_task.pending_compaction_tasks
            msg = 'Sending UVE: ' + str(cassandra_status_uve)
            self.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
            cassandra_status_uve.send()
    # end send_database_status

    def has_cassandra_status_changed(self,current_status, old_status):
        if current_status.cassandra_compaction_task.pending_compaction_tasks != \
            old_status.cassandra_compaction_task.pending_compaction_tasks :
            return True
        i = 0
        if len(current_status.thread_pool_stats) != \
            len(old_status.thread_pool_stats):
            return True
        while i < len(current_status.thread_pool_stats):
            if (current_status.thread_pool_stats[i].active != \
                old_status.thread_pool_stats[i].active or
                current_status.thread_pool_stats[i].pending != \
                old_status.thread_pool_stats[i].pending or
                current_status.thread_pool_stats[i].all_time_blocked != \
                old_status.thread_pool_stats[i].all_time_blocked):
                return True
            i = i+1
        return False
    # end has_cassandra_status_changed

    def get_pending_compaction_count(self, pending_count):
        compaction_count_val = pending_count.strip()
        # output is of the format pending tasks: x
        pending_count_val = compaction_count_val.split(':')
        return int(pending_count_val[1].strip())
    # end get_pending_compaction_count

    def get_tp_status(self,tp_stats_output):
        tpstats_rows = tp_stats_output.split('\n')
        thread_pool_stats_list = []
        for row_index in range(1, len(tpstats_rows)):
            cols = tpstats_rows[row_index].split()
            # If tpstats len(cols) > 2, else we have reached the end
            if len(cols) > 2:
                if (cols[0] in ThreadPoolNames):
                    # Create a CassandraThreadPoolStats for matching entries
                    tpstat = CassandraThreadPoolStats()
                    tpstat.pool_name = cols[0]
                    tpstat.active = int(cols[1])
                    tpstat.pending = int(cols[2])
                    tpstat.all_time_blocked = int(cols[5])
                    thread_pool_stats_list.append(tpstat)
            else:
                # Reached end of tpstats, breaking because dropstats follows
                break
        return thread_pool_stats_list
    # end get_tp_status

    def do_periodic_events(self):
        self.database_periodic()
        self.event_tick_60()
    # end do_periodic_events
# end class DatabaseEventManager
