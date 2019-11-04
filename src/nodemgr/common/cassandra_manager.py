# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.#
from gevent import monkey

import socket
import yaml
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import ThreadPoolNames,\
    SERVICE_CONTRAIL_DATABASE
from sandesh_common.vns.ttypes import Module
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from database.sandesh.database.ttypes import CassandraThreadPoolStats,\
    CassandraStatusUVE, CassandraStatusData, CassandraThreadPoolStats,\
    CassandraCompactionTask, DatabaseUsageStats, DatabaseUsageInfo,\
    DatabaseUsage
from sandesh_common.vns.constants import RepairNeededKeyspaces,\
            AnalyticsRepairNeededKeyspaces

monkey.patch_all()


class CassandraManager(object):
    def __init__(self, cassandra_repair_logdir, db_owner, table,
                 hostip, minimum_diskgb, db_port, db_jmx_port, db_use_ssl,
                 db_user, db_password, process_info_manager, hostname=None):
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self._db_owner = db_owner
        self.table = table
        self.hostip = hostip
        self.hostname = socket.getfqdn(self.hostip) if hostname is None \
            else hostname
        self.minimum_diskgb = minimum_diskgb
        self.db_port = db_port
        self.db_jmx_port = db_jmx_port
        self.db_use_ssl = db_use_ssl
        self.db_user = db_user
        self.db_password = db_password
        self.process_info_manager = process_info_manager
        # Initialize tpstat structures
        self.status_old = CassandraStatusData()
        self.status_old.cassandra_compaction_task = CassandraCompactionTask()
        self.status_old.thread_pool_stats = []

    def status(self):
        # TODO: here was a call to contrail-cassandra-status utility
        # it could stop cassandra's service but this tool is not present
        # and this is not allowed in micrioservices setup
        pass

    def repair(self, event_mgr):
        keyspaces = []
        if self._db_owner == 'analytics':
            keyspaces = AnalyticsRepairNeededKeyspaces
        elif self._db_owner == 'config':
            keyspaces = RepairNeededKeyspaces
        for keyspace in keyspaces:
            cmd = "nodetool -p {} repair -pr {}".format(self.db_jmx_port, keyspace)
            try:
                res = self.exec_cmd(cmd)
                event_mgr.msg_log(res, level=SandeshLevel.SYS_DEBUG)
            except Exception as e:
                err_msg = "Failed to run cmd: '{}'. Error is: {}".format(cmd, e)
                event_mgr.msg_log(err_msg, level=SandeshLevel.SYS_ERR)

    def exec_cmd(self, cmd):
        # unit name must be equal to definition in main.py
        unit_name = 'cassandra'
        return self.process_info_manager.exec_cmd(unit_name, cmd)

    def _get_cassandra_config_option(self, config):
        # NOTE: assume that we have debian-based installation of cassandra
        raw_cfg = self.exec_cmd('cat /etc/cassandra/cassandra.yaml')
        cfg = yaml.load(raw_cfg)
        return cfg[config]

    def disk_free(self, cdir):
        output = self.exec_cmd("df " + cdir)
        _, _, used, available, _, _ = output.split("\n")[1].split()
        return (used, available)

    def disk_usage(self, cdir):
        output = self.exec_cmd("du -skl " + cdir)
        usage, _ = output.split()
        return usage

    def get_tp_status(self, tp_stats_output):
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

    def has_cassandra_status_changed(self, current_status, old_status):
        if (current_status.cassandra_compaction_task.pending_compaction_tasks
                != old_status.cassandra_compaction_task.pending_compaction_tasks):
            return True
        i = 0
        if len(current_status.thread_pool_stats) != \
                len(old_status.thread_pool_stats):
            return True
        while i < len(current_status.thread_pool_stats):
            if (current_status.thread_pool_stats[i].active
                    != old_status.thread_pool_stats[i].active
                    or current_status.thread_pool_stats[i].pending
                    != old_status.thread_pool_stats[i].pending
                    or current_status.thread_pool_stats[i].all_time_blocked
                    != old_status.thread_pool_stats[i].all_time_blocked):
                return True
            i += 1
        return False
    # end has_cassandra_status_changed

    def get_pending_compaction_count(self, pending_count_output):
        lines = pending_count_output.split('\n')
        pending_count = next(iter(
            [i for i in lines if i.startswith('pending tasks:')]), None)
        compaction_count_val = pending_count.strip()
        # output is of the format pending tasks: x
        pending_count_val = compaction_count_val.split(':')
        return int(pending_count_val[1].strip())
    # end get_pending_compaction_count

    def database_periodic(self, event_mgr):
        try:
            cassandra_data_dirs = self._get_cassandra_config_option("data_file_directories")
            cassandra_data_dir_exists = False
            total_disk_space_used = 0
            total_disk_space_available = 0
            total_db_size = 0
            for cdir in cassandra_data_dirs:
                cassandra_data_dir_exists = True
                msg = "dir for " + self._db_owner + " is " + cdir
                event_mgr.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
                (disk_space_used, disk_space_available) = (
                    self.disk_free(cdir))
                total_disk_space_used += int(disk_space_used)
                total_disk_space_available += int(disk_space_available)
                db_size = self.disk_usage(cdir)
                total_db_size += int(db_size)

            if not cassandra_data_dir_exists:
                event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE_NA
            else:
                event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_DISK_SPACE_NA

                disk_space = int(total_disk_space_used) + int(total_disk_space_available)
                if (disk_space / (1024 * 1024) < self.minimum_diskgb):
                    # TODO: here was a call that stops cassandra's service
                    event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE

                db_stat = DatabaseUsageStats()
                db_info = DatabaseUsageInfo()

                db_stat.disk_space_used_1k = int(total_disk_space_used)
                db_stat.disk_space_available_1k = int(total_disk_space_available)
                if self._db_owner == 'analytics':
                    db_stat.analytics_db_size_1k = int(total_db_size)
                elif self._db_owner == 'config':
                    db_stat.config_db_size_1k = int(total_db_size)

                db_info.name = self.hostname
                db_info.database_usage = [db_stat]
                usage_stat = DatabaseUsage(table=self.table, data=db_info)
                usage_stat.send()
        except Exception as e:
            msg = "Failed to get database usage: " + str(e)
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_DISK_SPACE_NA

        # just check connectivity
        cqlsh_cmd = "cqlsh"
        if self.db_use_ssl:
            cqlsh_cmd += " --ssl"
        if (self.db_user and self.db_password):
            cqlsh_cmd += " -u {} -p {}".format(self.db_user, self.db_password)
        cqlsh_cmd += " {} {} -e quit".format(self.hostip, self.db_port)
        try:
            self.exec_cmd(cqlsh_cmd)
            event_mgr.fail_status_bits &= ~event_mgr.FAIL_STATUS_SERVER_PORT
        except Exception as e:
            msg = "Failed to connect to database by CQL: " + str(e)
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            event_mgr.fail_status_bits |= event_mgr.FAIL_STATUS_SERVER_PORT
        event_mgr.send_nodemgr_process_status()
        # Send cassandra nodetool information
        self.send_database_status(event_mgr)
        # Record cluster status and shut down cassandra if needed
        self.status()
    # end database_periodic

    def send_database_status(self, event_mgr):
        status = CassandraStatusData()
        status.cassandra_compaction_task = CassandraCompactionTask()
        # Get compactionstats
        base_cmd = "nodetool -p {}".format(self.db_jmx_port)
        try:
            res = self.exec_cmd(base_cmd + " compactionstats")
            status.cassandra_compaction_task.pending_compaction_tasks = \
                self.get_pending_compaction_count(res)
        except Exception as e:
            msg = "Failed to get nodetool compactionstats: {}".format(e)
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            return

        # Get the tpstats value
        try:
            res = self.exec_cmd(base_cmd + " tpstats")
            status.thread_pool_stats = self.get_tp_status(res)
        except Exception as e:
            msg = "Failed to get nodetool tpstats {}".format(e)
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_ERR)
            return

        status.name = self.hostname
        status_uve = CassandraStatusUVE(table=self.table, data=status)
        if self.has_cassandra_status_changed(status, self.status_old):
            # Assign status to status_old
            self.status_old.thread_pool_stats = status.thread_pool_stats
            self.status_old.cassandra_compaction_task.\
                pending_compaction_tasks = status.\
                cassandra_compaction_task.pending_compaction_tasks
            msg = 'Sending UVE: ' + str(status_uve)
            event_mgr.msg_log(msg, level=SandeshLevel.SYS_DEBUG)
            status_uve.send()
    # end send_database_status

    def get_failbits_nodespecific_desc(self, event_mgr, fail_status_bits):
        description = []
        if fail_status_bits & event_mgr.FAIL_STATUS_DISK_SPACE:
            description.append("Disk for DB is too low.")
        if fail_status_bits & event_mgr.FAIL_STATUS_SERVER_PORT:
            description.append("Cassandra state detected DOWN.")
        if fail_status_bits & event_mgr.FAIL_STATUS_DISK_SPACE_NA:
            description.append("Disk space for DB not retrievable.")
        return description
