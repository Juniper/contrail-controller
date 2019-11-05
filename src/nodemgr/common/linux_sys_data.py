#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import subprocess
import psutil

from nodemgr.common.common_sys_cpu import SysCpuShare
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import SysMemInfo, SysCpuInfo, CpuLoadAvg
from nodemgr.common.sandesh.nodeinfo.process_info.ttypes import DiskPartitionUsageStats


class LinuxSysData(object):
    def __init__(self, msg_log, corefile_path):
        self.msg_log = msg_log
        self.corefile_path = corefile_path
        self.core_dir_modified_time = 0
        self.num_socket = None
        self.num_cpu = None
        self.num_core_per_socket = None
        self.num_thread_per_core = None
        self.sys_cpu_share = SysCpuShare(self.get_num_cpu())

    @staticmethod
    def _run_cmd(cmd):
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, close_fds=True)
        return int(proc.communicate()[0])

    def get_num_socket(self):
        if self.num_socket is not None:
            return self.num_socket
        self.num_socket = LinuxSysData._run_cmd(
            'lscpu | grep "Socket(s):" | awk \'{print $2}\'')
        return self.num_socket

    def get_num_cpu(self):
        if self.num_cpu is not None:
            return self.num_cpu
        self.num_cpu = LinuxSysData._run_cmd(
            'lscpu | grep "^CPU(s):" | awk \'{print $2}\'')
        return self.num_cpu

    def get_num_core_per_socket(self):
        if self.num_core_per_socket is not None:
            return self.num_core_per_socket
        self.num_core_per_socket = LinuxSysData._run_cmd(
            'lscpu | grep "Core(s) per socket:" | awk \'{print $4}\'')
        return self.num_core_per_socket

    def get_num_thread_per_core(self):
        if self.num_thread_per_core is not None:
            return self.num_thread_per_core
        self.num_thread_per_core = LinuxSysData._run_cmd(
            'lscpu | grep "Thread(s) per core:" | awk \'{print $4}\'')
        return self.num_thread_per_core

    def get_sys_mem_info(self, node_type):
        virtmem_info = psutil.virtual_memory()
        sys_mem_info = SysMemInfo()
        sys_mem_info.total = virtmem_info.total // 1024
        sys_mem_info.used = virtmem_info.used // 1024
        sys_mem_info.free = virtmem_info.free // 1024
        sys_mem_info.buffers = virtmem_info.buffers // 1024
        sys_mem_info.cached = virtmem_info.cached // 1024
        sys_mem_info.node_type = node_type
        return sys_mem_info

    def get_sys_cpu_info(self, node_type):
        cpu_load_avg = self._get_sys_cpu_load_avg()
        sys_cpu_info = SysCpuInfo()
        sys_cpu_info.one_min_avg = cpu_load_avg.one_min_avg
        sys_cpu_info.five_min_avg = cpu_load_avg.five_min_avg
        sys_cpu_info.fifteen_min_avg = cpu_load_avg.fifteen_min_avg
        sys_cpu_info.cpu_share = self.sys_cpu_share.get()
        sys_cpu_info.node_type = node_type
        return sys_cpu_info

    def _get_sys_cpu_load_avg(self):
        load_avg = os.getloadavg()
        cpu_load_avg = CpuLoadAvg()
        cpu_load_avg.one_min_avg = load_avg[0]
        cpu_load_avg.five_min_avg = load_avg[1]
        cpu_load_avg.fifteen_min_avg = load_avg[2]
        return cpu_load_avg

    def check_ntp_status(self):
        # chronyc fails faster - it should be first
        ntp_status_cmds = [
            'chronyc -n sources | grep "^\^\*"',
            'ntpq -n -c pe | grep "^\*"',
        ]
        for cmd in ntp_status_cmds:
            # TODO: cache chosen method and use it later
            proc = subprocess.Popen(
                cmd, shell=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
            (_, _) = proc.communicate()
            if proc.returncode == 0:
                return True
        return False

    def _get_corefile_path(self):
        result = self.corefile_path
        with open('/proc/sys/kernel/core_pattern', 'r') as fh:
            try:
                core_pattern = fh.readline().strip('\n')
                if core_pattern is not None and not core_pattern.startswith('|'):
                    result = os.path.dirname(core_pattern)
            except Exception:
                pass
        return result

    def get_corefiles(self):
        try:
            corefile_path = self._get_corefile_path()
            exception_set = {"lost+found"}
            files = filter(os.path.isfile, os.listdir(corefile_path))
            files = [os.path.join(corefile_path, f) for f in files if f not in exception_set]
            files.sort(key=lambda x: os.path.getmtime(x))
            return files
        except Exception as e:
            self.msg_log('Failed to get core files: %s' % (str(e)),
                         SandeshLevel.SYS_ERR)
            return list()

    def remove_corefiles(self, core_files):
        for core_file in core_files:
            self.msg_log('deleting core file: %s' % (core_file),
                         SandeshLevel.SYS_ERR)
            try:
                os.remove(core_file)
            except OSError as e:
                self.msg_log('Failed to delete core file: %s - %s'
                             % (core_file, str(e)),
                             SandeshLevel.SYS_ERR)

    def update_all_core_file(self):
        try:
            modified_time = os.path.getmtime(self._get_corefile_path())
        except OSError:
            # folder is not present - corefiles have not been changed
            return False
        if modified_time == self.core_dir_modified_time:
            return False
        self.core_dir_modified_time = modified_time
        self.all_core_file_list = self.get_corefiles()
        return True

    def get_disk_usage(self):
        disk_usage_info = {}
        partition = subprocess.Popen(
            "df -PT -t ext2 -t ext3 -t ext4 -t xfs",
            shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            close_fds=True)
        for line in partition.stdout:
            line = line.decode()
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
                    int(round((float(disk_usage_stat.partition_space_used_1k)
                               / float(total_disk_space)) * 100))
            except ValueError:
                self.msg_log('Failed to get local disk space usage',
                             SandeshLevel.SYS_ERR)
            else:
                disk_usage_info[partition_name] = disk_usage_stat
        return disk_usage_info
