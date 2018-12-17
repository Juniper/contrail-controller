#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import subprocess
import psutil

from common_sys_cpu import SysCpuShare
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.nodeinfo.cpuinfo.ttypes import SysMemInfo, SysCpuInfo, CpuLoadAvg
from sandesh.nodeinfo.process_info.ttypes import DiskPartitionUsageStats


class LinuxSysData(object):
    def __init__(self, msg_log, corefile_path):
        self.msg_log = msg_log
        self.corefile_path = corefile_path
        self.core_dir_modified_time = 0
        self.sys_cpu_share = SysCpuShare(self.get_num_cpu())

    @staticmethod
    def _run_cmd(cmd):
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, close_fds=True)
        return int(proc.communicate()[0])

    def get_num_socket(self):
        return LinuxSysData._run_cmd(
            'lscpu | grep "Socket(s):" | awk \'{print $2}\'')

    def get_num_cpu(self):
        return LinuxSysData._run_cmd(
            'lscpu | grep "^CPU(s):" | awk \'{print $2}\'')

    def get_num_core_per_socket(self):
        return LinuxSysData._run_cmd(
            'lscpu | grep "Core(s) per socket:" | awk \'{print $4}\'')

    def get_num_thread_per_core(self):
        return LinuxSysData._run_cmd(
            'lscpu | grep "Thread(s) per core:" | awk \'{print $4}\'')

    def get_sys_mem_info(self, node_type):
        virtmem_info = psutil.virtual_memory()
        sys_mem_info = SysMemInfo()
        sys_mem_info.total = virtmem_info.total / 1024
        sys_mem_info.used = virtmem_info.used / 1024
        sys_mem_info.free = virtmem_info.free / 1024
        sys_mem_info.buffers = virtmem_info.buffers / 1024
        sys_mem_info.cached = virtmem_info.cached / 1024
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
            proc = subprocess.Popen(
                cmd, shell=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
            (_, _) = proc.communicate()
            if proc.returncode == 0:
                return True
        return False

    def _get_corefile_path(self):
        self.core_file_path = self.corefile_path
        cat_command = "cat /proc/sys/kernel/core_pattern"
        (core_pattern, _) = subprocess.Popen(
                cat_command.split(),
                stdout=subprocess.PIPE, close_fds=True).communicate()
        if core_pattern is not None and not core_pattern.startswith('|'):
            dirname_cmd = "dirname " + core_pattern
            (self.core_file_path, _) = subprocess.Popen(
                dirname_cmd.split(),
                stdout=subprocess.PIPE, close_fds=True).communicate()
        return self.core_file_path.rstrip()

    def find_corefile(self, name_pattern):
        find_command_option = (
            "find " + self._get_corefile_path()
            + " -name " + name_pattern)
        (corename, _) = subprocess.Popen(
            find_command_option.split(),
            stdout=subprocess.PIPE, close_fds=True).communicate()
        return corename

    def get_corefiles(self):
        try:
            # Get the core files list in the chronological order
            ls_command = "ls -1tr " + self._get_corefile_path()
            (corenames, _) = subprocess.Popen(
                ls_command.split(),
                stdout=subprocess.PIPE, close_fds=True).communicate()
        except Exception as e:
            self.msg_log('Failed to get core files: %s' % (str(e)),
                SandeshLevel.SYS_ERR)
        else:
            return [self._get_corefile_path() + '/' + core
                    for core in corenames.split()]

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
        stat_command_option = "stat --printf=%Y " + self._get_corefile_path()
        modified_time = subprocess.Popen(
            stat_command_option.split(),
            stdout=subprocess.PIPE, close_fds=True).communicate()
        if modified_time[0] == self.core_dir_modified_time:
            return False
        self.core_dir_modified_time = modified_time[0]
        self.all_core_file_list = self.get_corefiles()
        return True

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
                    int(round((float(disk_usage_stat.partition_space_used_1k) / \
                               float(total_disk_space)) * 100))
            except ValueError:
                self.msg_log('Failed to get local disk space usage',
                             SandeshLevel.SYS_ERR)
            else:
                disk_usage_info[partition_name] = disk_usage_stat
        return disk_usage_info
