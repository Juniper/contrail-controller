
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.#
import os
from gevent import monkey
monkey.patch_all()

import socket
import subprocess
import platform
import yaml
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import ThreadPoolNames,\
    SERVICE_CONTRAIL_DATABASE
from sandesh_common.vns.ttypes import Module
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from database.sandesh.database.ttypes import CassandraThreadPoolStats,\
    CassandraStatusUVE,CassandraStatusData,CassandraThreadPoolStats,\
    CassandraCompactionTask, DatabaseUsageStats, DatabaseUsageInfo,\
    DatabaseUsage


class CassandraManager(object):
    def __init__(self, cassandra_repair_logdir, db_name, table, contrail_databases,
                 hostip, minimum_diskgb, db_port):
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self._db_name = db_name
        self.table = table
        self.contrail_databases = contrail_databases
        self.hostip = hostip
        self.db_port = db_port
        self.minimum_diskgb = minimum_diskgb
        # Initialize tpstat structures
        self.cassandra_status_old = CassandraStatusData()
        self.cassandra_status_old.cassandra_compaction_task = CassandraCompactionTask()
        self.cassandra_status_old.thread_pool_stats = []

    def status(self):
        subprocess.Popen(["contrail-cassandra-status",
                          "--log-file", "/var/log/cassandra/status.log",
                          "--debug"], close_fds=True)

    def repair(self):
        logdir = self.cassandra_repair_logdir + "repair.log"
        subprocess.Popen(["contrail-cassandra-repair",
                          "--log-file", logdir,
                          "--debug"], close_fds=True)

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

    def disk_space_helper(self, df_dir):
        df = subprocess.Popen(["df", df_dir],
                stdout=subprocess.PIPE, close_fds=True)
        output = df.communicate()[0]
        device, size, disk_space_used, disk_space_available, \
           percent, mountpoint = output.split("\n")[1].split()
        return (disk_space_used, disk_space_available)

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

    def get_pending_compaction_count(self, pending_count):
        compaction_count_val = pending_count.strip()
        # output is of the format pending tasks: x
        pending_count_val = compaction_count_val.split(':')
        return int(pending_count_val[1].strip())
    # end get_pending_compaction_count

    def process(self, event_mgr):
        try:
            cassandra_data_dirs = self._get_cassandra_config_option("data_file_directories")
            cassandra_data_dir_exists = False
            total_disk_space_used = 0
            total_disk_space_available = 0
            for cassandra_data_dir in cassandra_data_dirs:
                if CassandraManager.cassandra_old():
                    analytics_dir = cassandra_data_dir + '/ContrailAnalytics'
                else:
                    analytics_dir = cassandra_data_dir + '/ContrailAnalyticsCql'

                if self._db_name == 'analyticsDb' and os.path.exists(analytics_dir):
                    cassandra_data_dir_exists = True
                    msg = "analytics_dir is " + analytics_dir
                    event_mgr.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
                    (disk_space_used, disk_space_available) = (self.
                                               disk_space_helper(analytics_dir))
                    total_disk_space_used += int(disk_space_used)
                    total_disk_space_available += int(disk_space_available)
                elif os.path.exists(cassandra_data_dir) and self._db_name == 'configDb':
                    cassandra_data_dir_exists = True
                    msg = "cassandra_dir is " + cassandra_data_dir
                    event_mgr.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
                    (disk_space_used, disk_space_available) = (self.
                                               disk_space_helper(cassandra_data_dir))
                    total_disk_space_used += int(disk_space_used)
                    total_disk_space_available += int(disk_space_available)
            if cassandra_data_dir_exists == False:
                if ((self._db_name == 'analyticsDb' and
                          'analytics' not in self.contrail_databases) or
                    (self._db_name == 'configDb' and
                          'config' not in self.contrail_databases)):
                    event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_DISK_SPACE_NA
                else:
                    event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE_NA
            else:
                disk_space = int(total_disk_space_used) + int(total_disk_space_available)
                if (disk_space / (1024 * 1024) < self.minimum_diskgb):
                    cmd_str = "service " + SERVICE_CONTRAIL_DATABASE + " stop"
                    (ret_value, error_value) = subprocess.Popen(
                        cmd_str, shell=True, stdout=subprocess.PIPE,
                        close_fds=True).communicate()
                    event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE
                event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_DISK_SPACE_NA
        except:
            msg = "Failed to get database usage"
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE_NA

    def database_periodic(self, event_mgr):
        try:
            cassandra_data_dirs = self._get_cassandra_config_option("data_file_directories")
            cassandra_data_dir_exists = False
            total_disk_space_used = 0
            total_disk_space_available = 0
            total_db_size = 0
            for cassandra_data_dir in cassandra_data_dirs:
                if CassandraManager.cassandra_old():
                    analytics_dir = cassandra_data_dir + '/ContrailAnalytics'
                else:
                    analytics_dir = cassandra_data_dir + '/ContrailAnalyticsCql'

                if self._db_name == 'analyticsDb' and os.path.exists(analytics_dir):
                    cassandra_data_dir_exists = True
                    msg = "analytics_dir is " + analytics_dir
                    event_mgr.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
                    (disk_space_used, disk_space_available) = (self.
                                               disk_space_helper(analytics_dir))
                    total_disk_space_used += int(disk_space_used)
                    total_disk_space_available += int(disk_space_available)
                    du = subprocess.Popen(["du", "-skl", analytics_dir],
                            stdout=subprocess.PIPE, close_fds=True)
                    db_size, directory = du.communicate()[0].split()
                    total_db_size += int(db_size)
                elif os.path.exists(cassandra_data_dir) and self._db_name == 'configDb':
                    cassandra_data_dir_exists = True
                    msg = "cassandra_dir is " + cassandra_data_dir
                    (disk_space_used, disk_space_available) = (self.
                                               disk_space_helper(cassandra_data_dir))
                    total_disk_space_used += int(disk_space_used)
                    total_disk_space_available += int(disk_space_available)
                    du = subprocess.Popen(["du", "-skl", cassandra_data_dir],
                            stdout=subprocess.PIPE, close_fds=True)
                    db_size, directory = du.communicate()[0].split()
                    total_db_size += int(db_size)
            if cassandra_data_dir_exists == False:
                if ((self._db_name == 'analyticsDb' and
                          'analytics' not in self.contrail_databases) or
                    (self._db_name == 'configDb' and
                          'config' not in self.contrail_databases)):
                    event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_DISK_SPACE_NA
                else:
                    event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE_NA
            else:
                event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_DISK_SPACE_NA

                db_stat = DatabaseUsageStats()
                db_info = DatabaseUsageInfo()

                db_stat.disk_space_used_1k = int(total_disk_space_used)
                db_stat.disk_space_available_1k = int(total_disk_space_available)
                if self._db_name == 'analyticsDb':
                    db_stat.analytics_db_size_1k = int(total_db_size)
                elif self._db_name == 'configDb':
                    db_stat.config_db_size_1k = int(total_db_size)

                db_info.name = socket.gethostname()
                db_info.database_usage = [db_stat]
                usage_stat = DatabaseUsage(table=self.table, data=db_info)
                usage_stat.send()
        except:
            msg = "Failed to get database usage"
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE_NA

        cqlsh_cmd = "cqlsh " + self.hostip + " " + self.db_port + " -e quit"
        proc = subprocess.Popen(cqlsh_cmd, shell=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, close_fds=True)
        (output, errout) = proc.communicate()
        if proc.returncode != 0:
            event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_SERVER_PORT
        else:
            event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_SERVER_PORT
        event_mgr.send_nodemgr_process_status()
        # Send cassandra nodetool information
        self.send_database_status(event_mgr)
        # Record cluster status and shut down cassandra if needed
        self.status()
    # end database_periodic

    def send_database_status(self, event_mgr):
        cassandra_status = CassandraStatusData()
        cassandra_status.cassandra_compaction_task = CassandraCompactionTask()
        # Get compactionstats
        compaction_count = subprocess.Popen("nodetool compactionstats|grep 'pending tasks:'",
            shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            close_fds=True)
        op, err = compaction_count.communicate()
        if compaction_count.returncode != 0:
            msg = "Failed to get nodetool compactionstats " + err
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            return
        cassandra_status.cassandra_compaction_task.pending_compaction_tasks = \
            self.get_pending_compaction_count(op)
        # Get the tpstats value
        tpstats_op = subprocess.Popen(["nodetool", "tpstats"], stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, close_fds=True)
        op, err = tpstats_op.communicate()
        if tpstats_op.returncode != 0:
            msg = "Failed to get nodetool tpstats " + err
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            return
        cassandra_status.thread_pool_stats = self.get_tp_status(op)
        cassandra_status.name = socket.gethostname()
        cassandra_status_uve = CassandraStatusUVE(table=self.table, data=cassandra_status)
        msg = 'Sending UVE: ' + str(cassandra_status_uve)
        event_mgr.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
        cassandra_status_uve.send()
    # end send_database_status
