#!/usr/bin/python

doc = """\
Storage Compute manager


"""
import sys
from gevent import monkey
from gevent.subprocess import Popen, PIPE
monkey.patch_all()
import os
import glob
import socket
import subprocess
import json
import time
import datetime
import pdb
import re
import argparse
import ConfigParser
import signal
import syslog

global HOME_ENV_PATH
HOME_ENV_PATH = '/root'


from stats_daemon.sandesh.storage.ttypes import *
from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType, INSTANCE_ID_DEFAULT
from sandesh_common.vns.constants import *


def usage():
    print doc
    sys.exit(255)
'''
EventManager class is used to creates events and send the same to \
 sandesh server(opserver)
'''


class osdMap:
    osd_disk = ''
    osd = ''
    osd_journal = ''


class diskUsage:
    disk = ''
    disk_used = ''
    disk_avail = ''
    disk_size = ''

class prevOsdLatency:
    # primary osd read latency sum
    prev_op_rsum = 0
    # replica osd read latency sum
    prev_subop_rsum = 0
    # primary osd total read latency samples
    prev_op_rcount = 0
    # replica osd total read latency samples
    prev_subop_rcount = 0
    # primary osd write latency sum
    prev_op_wsum = 0
    # replica osd write latency sum
    prev_subop_wsum = 0
    # primary osd total write latency samples
    prev_op_wcount = 0
    # replica osd total write latency samples
    prev_subop_wcount = 0

class EventManager:
    rules_data = []
    headers = dict()
    process_state_db = {}
    prev_list = []

    def __init__(self, node_type):
        self.stdin = sys.stdin
        self.stdout = sys.stdout
        self.stderr = sys.stderr
        self.max_cores = 4
        self.max_old_cores = 3
        self.max_new_cores = 1
        self.node_type = node_type
        self._hostname = socket.gethostname()
        self.dict_of_osds = dict()
        self.prev_latency_dict = dict()
        self.units = self.init_units()
        self.curr_read_kbytes = 0
        self.curr_write_kbytes = 0
        self.curr_reads = 0
        self.curr_writes = 0
        self.curr_read_latency = 0
        self.curr_write_latency = 0
        pattern = 'rm -rf ceph.conf; ln -s /etc/ceph/ceph.conf ceph.conf'
        self.call_subprocess(pattern)

    def exec_local(self, arg):
        ret = Popen('%s' %(arg), shell=True,
                                stdout=PIPE).stdout.read()
        ret = ret[:-1]
        return ret

    def cleanup_pid(self, pid):
        pid_list = []
        proc_run = self.exec_local('ps -ef | grep -w %s | grep -v grep | wc -l'
                                    %(pid))
        if proc_run != '0':
            my_pid = os.getpid()
            procs = self.exec_local('ps -ef | grep -w %s | grep -v grep'
                                    %(pid))
            lines = procs.splitlines()
            for line in lines:
                pid = line.split()[1]
                pid_list.append(pid)
            while len(pid_list) != 0:
                for pid in pid_list:
                    running = self.exec_local('ps -ef | grep -w %s | \
                                    grep -v grep | awk \'{print $2}\' | \
                                    grep -w %s | wc -l' %(pid, pid))
                    if running != '0':
                        self.exec_local('kill -9 %s' %(pid))
                    else:
                        pid_list.remove(pid)
                time.sleep(5)
    #end cleanup_pid

    def init_units(self):
        units = dict();
        units['K'] = 1024
        units['M'] = int(units['K']) * 1024
        units['G'] = int(units['M']) * 1024
        units['T'] = int(units['G']) * 1024
        units['P'] = int(units['T']) * 1024
        return units

    '''
        This function is a wrapper for subprocess call. Timeout functionality
        is used to timeout after 5 seconds of no response from subprocess call
        and the corresponding cmd will be logged into syslog
    '''
    def call_subprocess(self, cmd):
        times = datetime.datetime.now()
        # latest 14.0.4 requires "HOME" env variable to be passed
        # copy current environment variables and add "HOME" variable
        # pass the newly created environment variable to Popen subprocess
        env_home = os.environ.copy()
        env_home['HOME'] = HOME_ENV_PATH
        # stdout and stderr are redirected.
        # stderr not used (stdout validation is done so stderr check is
        # is not needed)
        try:
            p = Popen(cmd, stdout=PIPE, \
                stderr=PIPE, shell=True, env=env_home)
            while p.poll() is None:
                gevent.sleep(0.1)
                now = datetime.datetime.now()
                diff = now - times
                if diff.seconds > 5:
                    #os.kill(p.pid, signal.SIGKILL)
                    os.waitpid(-1, os.WNOHANG)
                    message = "command:" + cmd + " ---> hanged"
                    ssdlog = StorageStatsDaemonLog(message = message)
                    self.call_send(ssdlog)
                    self.cleanup_pid(p.pid)
                    return None
        except:
            pass
            return None
        # stdout is used
        return p.stdout.read()

    def call_send(self, send_inst):
        #sys.stderr.write('sending UVE:' +str(send_inst))
        send_inst.send()

    '''
    This function reads the ceph cluster status
    Parses the health status output and gets error reason if present \
    StorageCluster object created and information is Populated
    UVE send call invoked to send the StorageCluster object
    '''

    def create_and_send_cluster_stats(self, mon_id):
        res = self.call_subprocess('/usr/bin/ceph health detail | grep -v ^pg')
        if res is None:
            return
        cluster_id = self.exec_local('ceph --admin-daemon \
                          /var/run/ceph/ceph-mon.%s.asok quorum_status | \
                          grep fsid | \
                          cut -d \'"\'  -f4' %(mon_id))
        cluster_stats = StorageCluster()
        cluster_stats.cluster_id = cluster_id
        cluster_stats.name = cluster_id
        status = res.split(' ')[0]
        status_count = res.count(' ')
        if status_count >= 1:
            detail_info = res.split(' ', 1)[1]
            summary_info = res.split(' ', 1)[1].splitlines()[0]
            osd_stat = self.call_subprocess('/usr/bin/ceph osd dump | \
                          egrep -w \'(down|out)\' | cut -d \' \' -f 1')
            multiple = 0
            osd_string = ''
            if osd_stat != '':
                osd_entries = osd_stat.splitlines()
                for osd_entry in osd_entries:
                    if osd_entry != '':
                        if osd_string == '':
                            osd_string = osd_entry
                        else:
                            multiple = 1
                            osd_string = osd_string + ', ' + osd_entry
                if multiple == 0:
                    detail_info = detail_info + (' OSD %s is down' %(osd_string))
                    summary_info = summary_info + ('; OSD %s is down' %(osd_string))
                else:
                    detail_info = detail_info + (' OSDs %s are down' %(osd_string))
                    summary_info = summary_info + ('; OSDs %s are down' %(osd_string))
            detail_info = re.sub('\n', ';', detail_info)
        else:
            status = res.splitlines()[0]
            detail_info = ''
            summary_info = ''
        cluster_info = ClusterInfo()
        if status == 'HEALTH_OK':
            cluster_info.status = 0
        elif status == 'HEALTH_WARN':
            cluster_info.status = 1
        elif status == 'HEALTH_ERR':
            cluster_info.status = 2
        cluster_info.health_detail = detail_info
        cluster_info.health_summary = summary_info
        cluster_stats.info_stats = [cluster_info]
        cluster_stats_trace = StorageClusterTrace(data=cluster_stats)
        self.call_send(cluster_stats_trace)

    '''
    This function reads the ceph rados statistics.
    Parses this statistics output and gets the read_cnt/read_bytes \
    write_cnt/write_bytes. ComputeStoragePool object created and all \
    the above statictics are assigned
    UVE send call invoked to send the ComputeStoragePool object
    '''

    def create_and_send_pool_stats(self):
        res = self.call_subprocess('/usr/bin/rados df')
        if res is None:
            return
        arr = res.splitlines()
        for line in arr:
            if line != arr[0]:
                result = re.sub(
                    '\s+', ' ', line).strip()
                arr1 = result.split()
                READS = 7
                READKBS = 8
                WRITES = 9
                WRITEKBS = 10
                if len(arr1) == 10:
                    READS = 6
                    READKBS = 7
                    WRITES = 8
                    WRITEKBS = 9
                if arr1[0] != "total":
                    cs_pool = ComputeStoragePool()
                    cs_pool.name = self._hostname + ':' + arr1[0]
                    pool_stats = PoolStats()
                    pool_stats.reads = int(arr1[READS])
                    pool_stats.read_kbytes = int(arr1[READKBS])
                    pool_stats.writes = int(arr1[WRITES])
                    pool_stats.write_kbytes = int(arr1[WRITEKBS])
                    cs_pool.info_stats = [pool_stats]
                    pool_stats_trace = ComputeStoragePoolTrace(data=cs_pool)
                    self.call_send(pool_stats_trace)


    def populate_osd_total_stats(self, osdname, osd_stats, prev_osd_latency):
        ceph_name = "ceph-" + osdname + ".asok"
        cmd = "ceph --admin-daemon /var/run/ceph/" + ceph_name + \
              " perf dump | egrep  -w \"\\\"op_w\\\":|\\\"" + \
              "op_r\\\":|\\\"subop_r\\\":|\\\"subop_w\\\":|\\\"" + \
              "op_r_out_bytes\\\":|\\\"subop_r_out_bytes\\\":|" + \
              "\\\"op_w_in_bytes\\\":|\\\"subop_w_in_bytes\\\":\""
        try:
            res1 = self.call_subprocess(cmd)
            if res1 is None:
                return False
            osd_stats.stats_time = datetime.datetime.now()
            arr1 = res1.splitlines()
            for line1 in arr1:
                result = re.sub('\s+', ' ', line1).strip()
                line2 = result.split(":")
                if len(line2) != 0:
                    if line2[0].find('subop_r_out_bytes') != -1 or \
                        line2[0].find('op_r_out_bytes') != -1:
                        osd_stats.read_kbytes += int(
                            line2[1].rstrip(",").strip(' ')) / 1024
                    elif line2[0].find('subop_w_in_bytes') != -1 or \
                        line2[0].find('op_w_in_bytes') != -1:
                        osd_stats.write_kbytes += int(
                            line2[1].rstrip(",").strip(' ')) / 1024
                    elif line2[0].find('subop_r') != -1 or \
                        line2[0].find('op_r') != -1:
                        osd_stats.reads += int(
                            line2[1].rstrip(",").strip(' '))
                    elif line2[0].find('subop_w') != -1 or \
                        line2[0].find('op_w') != -1:
                        osd_stats.writes += int(
                            line2[1].rstrip(",").strip(' '))
        except:
            pass

        res2 = self.populate_osd_latency_stats(osdname,  osd_stats,
                                               prev_osd_latency)
        if res2 is None:
            return False
        return True

    def diff_read_kbytes(self, line, osd_stats, temp_osd_stats,
                         osd_prev_stats, diff_time):
        # 'line' format : " xyz,"
        self.curr_read_kbytes += int(line.rstrip(",").strip(' ')) / 1024
        temp_osd_stats.read_kbytes = self.curr_read_kbytes
        osd_stats.read_kbytes = self.curr_read_kbytes - \
                                      osd_prev_stats.read_kbytes
        osd_stats.read_kbytes = int(osd_stats.read_kbytes / diff_time)

    def diff_write_kbytes(self, line, osd_stats, temp_osd_stats,
                          osd_prev_stats, diff_time):
        # 'line' format : " xyz,"
        self.curr_write_kbytes += int(line.rstrip(",").strip(' ')) / 1024
        temp_osd_stats.write_kbytes = self.curr_write_kbytes
        osd_stats.write_kbytes = self.curr_write_kbytes - \
                                      osd_prev_stats.write_kbytes
        osd_stats.write_kbytes = int(osd_stats.write_kbytes / diff_time)

    def diff_read_cnt(self, line, osd_stats, temp_osd_stats,
                      osd_prev_stats, diff_time):
        # 'line' format : " xyz,"
        self.curr_reads += int(line.rstrip(",").strip(' '))
        temp_osd_stats.reads = self.curr_reads
        osd_stats.reads = self.curr_reads - \
                                      osd_prev_stats.reads
        osd_stats.reads = int(osd_stats.reads / diff_time)

    def diff_write_cnt(self, line, osd_stats, temp_osd_stats,
                       osd_prev_stats, diff_time):
        # 'line' format : " xyz,"
        self.curr_writes += int(line.rstrip(",").strip(' '))
        temp_osd_stats.writes = self.curr_writes
        osd_stats.writes = self.curr_writes - \
                                      osd_prev_stats.writes
        osd_stats.writes = int(osd_stats.writes / diff_time)


    def populate_osd_diff_stats(self, osdname, osd_stats,
                                temp_osd_stats, osd_prev_stats):
        ceph_name = "ceph-" + osdname + ".asok"
        cmd = "ceph --admin-daemon /var/run/ceph/" + ceph_name + \
              " perf dump | egrep  -w \"\\\"op_w\\\":|\\\"" + \
              "op_r\\\":|\\\"subop_r\\\":|\\\"subop_w\\\":|\\\"" + \
              "op_r_out_bytes\\\":|\\\"subop_r_out_bytes\\\":|" + \
              "\\\"op_w_in_bytes\\\":|\\\"subop_w_in_bytes\\\":\""
        try:
            res1 = self.call_subprocess(cmd)
            if res1 is None:
                return False
            stats_time = datetime.datetime.now()
            diff_time = stats_time - osd_prev_stats.stats_time
            fdiff_time = float(diff_time.seconds) + \
                            float(diff_time.microseconds)/1000000
            temp_osd_stats.stats_time = stats_time
            arr1 = res1.splitlines()
            for line1 in arr1:
                result = re.sub('\s+', ' ', line1).strip()
                line2 = result.split(":")
                if len(line2) != 0:
                    if line2[0].find('subop_r_out_bytes') != -1 or \
                        line2[0].find('op_r_out_bytes') != -1:
                        self.diff_read_kbytes(line2[1],
                                            osd_stats,
                                            temp_osd_stats,
                                            osd_prev_stats,
                                            fdiff_time)
                    elif line2[0].find('subop_w_in_bytes') != -1 or \
                        line2[0].find('op_w_in_bytes') != -1:
                        self.diff_write_kbytes(line2[1],
                                            osd_stats,
                                            temp_osd_stats,
                                            osd_prev_stats,
                                            fdiff_time)
                    elif line2[0].find('subop_r') != -1 or \
                        line2[0].find('op_r') != -1:
                        self.diff_read_cnt(line2[1],
                                            osd_stats,
                                            temp_osd_stats,
                                            osd_prev_stats,
                                            fdiff_time)
                    elif line2[0].find('subop_w') != -1 or \
                        line2[0].find('op_w') != -1:
                        self.diff_write_cnt(line2[1],
                                            osd_stats,
                                            temp_osd_stats,
                                            osd_prev_stats,
                                            fdiff_time)
        except:
            pass
        return True


    def compute_read_latency(self, arr, osd_stats,
                             prev_osd_latency, op_flag):
        # 'line' format : ['op_read_latency', 'avgcount', '2822,', 'sum', '240.2423},']

        avgcount = int(arr[2].rstrip(","))
        # 'arr' format : "'sum': xyz.yzw},"
        sum_rlatency = int(float(arr[4].rstrip("},")))

        # sum_rlatency is in seconds
        # multiplied by 1000 to convert seconds to milliseconds
        if avgcount != 0:
            # op_flag = 1 indicates replica osd read latency
            if op_flag == 1:
                if(avgcount > prev_osd_latency.prev_subop_rcount):
                    osd_stats.op_r_latency += ((sum_rlatency * 1000) - \
                        (prev_osd_latency.prev_subop_rsum * 1000)) / \
                        (avgcount - prev_osd_latency.prev_subop_rcount)
                prev_osd_latency.prev_subop_rsum = sum_rlatency
                prev_osd_latency.prev_subop_rcount = avgcount
            # op_flag = 2 indicates primary osd read latency
            if op_flag == 2:
                if(avgcount > prev_osd_latency.prev_op_rcount):
                    osd_stats.op_r_latency += ((sum_rlatency * 1000) - \
                        (prev_osd_latency.prev_op_rsum * 1000)) / \
                        (avgcount - prev_osd_latency.prev_op_rcount)
                prev_osd_latency.prev_op_rsum = sum_rlatency
                prev_osd_latency.prev_op_rcount = avgcount
        else:
            osd_stats.op_r_latency += 0
            # op_flag = 1 indicates replica osd read latency
            if op_flag == 1:
                prev_osd_latency.prev_subop_rsum = 0
                prev_osd_latency.prev_subop_rcount = 0
            # op_flag = 2 indicates primary osd read latency
            if op_flag == 2:
                prev_osd_latency.prev_op_rsum = 0
                prev_osd_latency.prev_op_rcount = 0

    def compute_write_latency(self, arr, osd_stats,
                              prev_osd_latency, op_flag):
        # 'line' format : ['op_read_latency', 'avgcount', '2822,', 'sum', '240.2423},']

        avgcount = int(arr[2].rstrip(","))
        # 'arr' format : "'sum': xyz.yzw},"
        sum_wlatency = int(float(arr[4].rstrip("},")))
        # sum_wlatency is in seconds
        # multiplied by 1000 to convert seconds to milliseconds
        if avgcount != 0:
            # op_flag = 1 indicates replica osd write latency
            if op_flag == 1:
                if(avgcount > prev_osd_latency.prev_subop_wcount):
                    osd_stats.op_w_latency += ((sum_wlatency * 1000) - \
                        (prev_osd_latency.prev_subop_wsum * 1000)) / \
                        (avgcount - prev_osd_latency.prev_subop_wcount)
                prev_osd_latency.prev_subop_wsum = sum_wlatency
                prev_osd_latency.prev_subop_wcount = avgcount
            # op_flag = 2 indicates primary osd write latency
            if op_flag == 2:
                if(avgcount > prev_osd_latency.prev_op_wcount):
                    osd_stats.op_w_latency += ((sum_wlatency * 1000) - \
                        (prev_osd_latency.prev_op_wsum * 1000)) / \
                        (avgcount - prev_osd_latency.prev_op_wcount)
                prev_osd_latency.prev_op_wsum = sum_wlatency
                prev_osd_latency.prev_op_wcount = avgcount
        else:
            osd_stats.op_w_latency += 0
            # op_flag = 1 indicates replica osd write latency
            if op_flag == 1:
                prev_osd_latency.prev_subop_wsum = 0
                prev_osd_latency.prev_subop_wcount = 0
            # op_flag = 2 indicates primary osd write latency
            if op_flag == 2:
                prev_osd_latency.prev_op_wsum = 0
                prev_osd_latency.prev_op_wcount = 0

    def populate_osd_latency_stats(self, osdname, osd_stats, prev_osd_latency):
       lat_list={"op_r_latency","subop_r_latency","op_w_latency","subop_w_latency"}
       for entry in lat_list:
           ceph_name = "ceph-" + osdname + ".asok"
           cmd = ('ceph --admin-daemon /var/run/ceph/%s perf dump | \
               egrep -A5 -w %s |tr \"\\\"\" \" \" | \
               awk \'BEGIN{start=0;title=\"\";avgcount=\"\";sum=\"\"} \
               {i=1;while (i<=NF) {if($i == \"{\"){start=1} \
               if($i == \"}\" && start==1){break} \
               if($i==\"%s\"){title=$i} \
               if($i==\"avgcount\"){i=i+2;avgcount=$i} \
               if($i==\"sum\"){i=i+2;sum=$i}i=i+1}} \
               END{print title \" avgcount \" avgcount \" sum \" sum}\''
               %(ceph_name, entry, entry))
           res = self.call_subprocess(cmd)
           if res is None:
               return False
           res.lstrip(' ')
           line = res.split(' ')
           # subop_r_latency: replica osd read latency value
           if line[0] == 'subop_r_latency':
               self.compute_read_latency(line,
                   osd_stats, prev_osd_latency, 1)
           # op_r_latency: primary osd read latency value
           elif line[0] == 'op_r_latency':
               self.compute_read_latency(line,
                   osd_stats, prev_osd_latency, 2)
           # subop_w_latency: replica osd write latency value
           elif line[0] == 'subop_w_latency':
               self.compute_write_latency(line,
                   osd_stats, prev_osd_latency, 1)
           # op_w_latency: primary osd write latency value
           elif line[0] == 'op_w_latency':
               self.compute_write_latency(line,
                   osd_stats, prev_osd_latency, 2)
       return True


    '''
    This function checks if an osd is active, if yes parses output of \
    osd dump ComputeStorageOsd object created and statictics are assigned
    UVE send call invoked to send the ComputeStorageOsd object
    '''

    def create_and_send_osd_stats(self):
        res = self.call_subprocess('ls /var/lib/ceph/osd')
        if res is None:
            return
        arr = res.splitlines()
        linecount = 0
        for line in arr:
            no_prev_osd = 0
            cmd = "cat /var/lib/ceph/osd/" + arr[linecount] + "/active"
            is_active = self.call_subprocess(cmd)
            if is_active is None:
                linecount = linecount + 1
                continue
            #instantiate osd and its state
            cs_osd = ComputeStorageOsd()
            cs_osd_state = ComputeStorageOsdState()
            osd_stats = OsdStats()
            temp_osd_stats = OsdStats()
            prev_osd_latency = prevOsdLatency()
            #initialize fields
            osd_stats.reads = 0
            osd_stats.writes = 0
            osd_stats.read_kbytes = 0
            osd_stats.write_kbytes = 0
            osd_stats.op_r_latency = 0
            osd_stats.op_w_latency = 0
            self.curr_read_kbytes = 0
            self.curr_write_kbytes = 0
            self.curr_reads = 0
            self.curr_writes = 0
            self.curr_read_latency = 0
            self.curr_write_latency = 0
            # osd state is active and not down
            if is_active == "ok\n":
                cs_osd_state.status = "active"
                num = arr[linecount].split('-')[1]
                uuid = self.exec_local('ceph --admin-daemon \
                            /var/run/ceph/ceph-osd.%s.asok status 2>/dev/null | \
                            grep osd_fsid  | awk \'{print $2}\' | \
                            cut -d \'"\' -f 2' %(num))
                if uuid is '':
                    linecount = linecount + 1
                    continue
                cs_osd.uuid = uuid.rstrip("\n")
                osd_prev_stats = self.dict_of_osds.get(
                    cs_osd.uuid)
                osd_name = "osd." + num
                cs_osd.name = self._hostname + ':' + osd_name
                if osd_prev_stats is None:
                    no_prev_osd = 1
                    rval = self.populate_osd_total_stats(osd_name,
                                                         osd_stats,
                                                         prev_osd_latency)
                else:
                    prev_osd_latency = self.prev_latency_dict.get(
                        cs_osd.uuid)
                    rval = self.populate_osd_diff_stats(osd_name, osd_stats,
                                                        temp_osd_stats,
                                                        osd_prev_stats)
                    if rval == False:
                        linecount = linecount + 1
                        continue
                    rval = self.populate_osd_latency_stats(osd_name,
                                                           osd_stats,
                                                           prev_osd_latency)
                if rval == False:
                    linecount = linecount + 1
                    continue
            else:
                cs_osd_state.status = "inactive"
            if no_prev_osd == 0:
                cs_osd.info_stats = [osd_stats]
                cs_osd.info_state = cs_osd_state
                osd_stats_trace = ComputeStorageOsdTrace(
                    data=cs_osd)
                self.call_send(osd_stats_trace)
                self.dict_of_osds[
                    cs_osd.uuid] = temp_osd_stats
                self.prev_latency_dict[cs_osd.uuid] = prev_osd_latency
            else:
                self.dict_of_osds[cs_osd.uuid] = osd_stats
                self.prev_latency_dict[cs_osd.uuid] = prev_osd_latency
            linecount = linecount + 1



    def find_osdmaplist(self, osd_map, disk):
        for osdmap_obj in osd_map:
            if osdmap_obj.osd_disk.find(disk) != -1:
                return 'y'
        return 'n'

    def find_diskusagelist(self, disk_usage, disk):
        for disk_usage_obj in disk_usage:
            if disk_usage_obj.disk.find(disk) != -1:
                return disk_usage_obj
        return None

    def compute_usage(self, disk_usage_obj, unit):
        if unit.isalpha():
            return long(float(disk_usage_obj.
                    disk_used.strip(unit)) * self.units[unit])
        return 0

    '''
    This function parses output of iostat and assigns statistice to \
    ComputeStorageDisk
    UVE send call invoked to send the ComputeStorageDisk object
    '''

    def create_and_send_disk_stats(self):
        # iostat to get the raw disk list
        cmd = 'iostat -x 4 2 | awk \'{arr[NR]=$0} \
            END{for(i=(NR/2)+1;i<NR;i++) { print arr[i] }}\''
        res = self.call_subprocess(cmd)
        if res is None:
            return
        disk_list = res.splitlines()
        # osd disk list to get the mapping of osd to
        # raw disk
        # cd to /etc/ceph so that ceph-deploy command logs output to
        # /var/log/ceph and not /root
        # pattern = 'cd /etc/ceph && ceph-deploy disk list %s 2>&1' \
        #             %(self._hostname)
        pattern = 'cat /proc/mounts | grep "\/var\/lib\/ceph\/osd\/ceph"'
        res = self.call_subprocess(pattern)
        if res is None:
            return
        osd_list = res.splitlines()
        osd_map = []
        for line in osd_list:
            arr1 = line.split()
            osd_map_obj = osdMap()
            osd_map_obj.osd_disk = arr1[0]
            osd_map_obj.osd = 'osd.%s' %(arr1[1].split('-')[1])
            cmd = 'ls -l %s/journal | awk \'{print $11}\'' %(arr1[1])
            journal_uuid = self.call_subprocess(cmd).strip('\r\n')
            cmd = 'ls -l %s | awk \'{print $11}\'' %(journal_uuid)
            journal_disk = self.call_subprocess(cmd).strip('\r\n')
            if journal_disk[0] != '/':
                journal_disk = '/dev/%s' %(journal_disk.split('/')[2])
            osd_map_obj.osd_journal = journal_disk
            osd_map.append(osd_map_obj)

        # df used to get the free space of all disks
        res1 = self.call_subprocess('df -hl')
        if res1 is None:
            return
        df_list = res1.splitlines()
        disk_usage = []
        for line in df_list:
            if line.find('sda') != -1 or \
                line.find('vda') != -1:
                # replace multiple spaces to single
                # space here
                result = re.sub(
                    '\s+', ' ', line).strip()
                arr1 = result.split()
                if arr1[5] == '/':
                    disk_usage_obj = diskUsage()
                    disk_usage_obj.disk = arr1[0]
                    disk_usage_obj.disk_size = arr1[1]
                    disk_usage_obj.disk_used = arr1[2]
                    disk_usage_obj.disk_avail = arr1[3]
                    disk_usage.append(disk_usage_obj)

                elif line.find('sd') != -1 or \
                    line.find('vd') != -1:
                    # replace multiple spaces to single
                    # space here
                    result = re.sub(
                        '\s+', ' ', line).strip()
                    arr1 = result.split()
                    disk_usage_obj = diskUsage()
                    disk_usage_obj.disk = arr1[0]
                    disk_usage_obj.disk_size = arr1[1]
                    disk_usage_obj.disk_used = arr1[2]
                    disk_usage_obj.disk_avail = arr1[3]
                    disk_usage.append(disk_usage_obj)

# create a dictionary of disk_name: model_num + serial_num
        new_dict = dict()
        resp = self.call_subprocess('ls -l /dev/disk/by-id/')
        if resp is None:
            return
        arr_disks = resp.splitlines()
        for line in arr_disks[1:]:
            resp1 = line.split()
            if (resp1[-1].find('sd') != -1 and
                resp1[8].find('part') == -1 and
                    resp1[8].find('ata') != -1):
                    new_dict[resp1[-1].split('/')[2]] = resp1[8]

        #cs_disk1 = ComputeStorageDisk()
        #cs_disk1.list_of_curr_disks = []
        for line in disk_list:       # this will have all rows
            # replace multiple spaces to single space here
            result = re.sub('\s+', ' ', line).strip()
            arr1 = result.split()
            if len(arr1) != 0 and (arr1[0].find('sd') != -1 or \
                arr1[0].find('vd') != -1):
                cs_disk = ComputeStorageDisk()
                cs_disk.name = self._hostname + ':' + arr1[0]
                osd_id = self.exec_local('cat /proc/mounts | \
                            grep %s | grep ceph | grep -v tmp | \
                            awk \'{print $2}\' | cut -d \'-\' -f 2'
                            %(arr1[0]))
                if osd_id == '':
                    cs_disk.uuid = ''
                else:
                    uuid = self.exec_local('ceph --admin-daemon \
                            /var/run/ceph/ceph-osd.%s.asok status 2>/dev/null | \
                            grep osd_fsid  | awk \'{print $2}\' | \
                            cut -d \'"\' -f 2' %(osd_id))
                    cs_disk.uuid = uuid
                #cs_disk1.list_of_curr_disks.append(arr1[0])
                cs_disk.is_osd_disk = self.find_osdmaplist(osd_map, arr1[0])
                disk_usage_obj = self.find_diskusagelist(disk_usage, arr1[0])
                if disk_usage_obj is None:
                    cs_disk.current_disk_usage = 0
                else:
                    last = disk_usage_obj.disk_used[-1:]
                    cs_disk.current_disk_usage = \
                        self.compute_usage(disk_usage_obj, last)
                disk_stats = DiskStats()
                disk_stats.reads = 0
                disk_stats.writes = 0
                disk_stats.read_kbytes = 0
                disk_stats.write_kbytes = 0
                disk_stats.op_r_latency = 0
                disk_stats.op_w_latency = 0
                if arr1[0] in new_dict:
                    if cs_disk.uuid == '':
                        cs_disk.uuid = new_dict.get(arr1[0])
                disk_stats.iops = int(float(arr1[3]) + float(arr1[4]))
                disk_stats.bw = int(float(arr1[5])) + \
                    int(float(arr1[6]))
                disk_stats.reads = int(float(arr1[3]))
                disk_stats.writes = int(float(arr1[4]))
                disk_stats.read_kbytes = int(float(arr1[5]))
                disk_stats.write_kbytes = int(float(arr1[6]))
                disk_stats.op_r_latency = int(float(arr1[10]))
                disk_stats.op_w_latency = int(float(arr1[11]))
                cs_disk.info_stats = [disk_stats]
                disk_stats_trace = ComputeStorageDiskTrace(data=cs_disk)
                self.call_send(disk_stats_trace)

        #cs_disk1_trace = ComputeStorageDiskTrace(data=cs_disk1)
        # sys.stderr.write('sending UVE:' +str(cs_disk1_trace))
        #if len(set(cs_disk1.list_of_curr_disks).
        #       difference(set(self.prev_list))) != 0:
        #    self.call_send(cs_disk1_trace)
        #self.prev_list = []
        #for i in xrange(0, len(cs_disk1.list_of_curr_disks)-1):
        #    self.prev_list.append(cs_disk1.list_of_curr_disks[i])

    # send UVE for updated process state database
    def send_process_state_db(self):
        sleep_time = 20
        # Check if the mon is the mon leader
        # Send pool stats and cluster stats from the mon leader alone
        mon_running = self.exec_local('ls /var/run/ceph/ceph-mon*.asok \
                                        2> /dev/null | wc -l')

        if mon_running != '0':
            mon_id = self.exec_local('ls  /var/run/ceph/ceph-mon*.asok | \
                                        cut -d \'.\'  -f 2')
            mon_leader = self.exec_local('ceph --admin-daemon \
                          /var/run/ceph/ceph-mon.%s.asok quorum_status | \
                          grep quorum_leader_name | \
                          cut -d \'"\'  -f4' %(mon_id))
            if mon_id == mon_leader:
                self.create_and_send_cluster_stats(mon_id)
                self.create_and_send_pool_stats()
                if self.node_type != "storage-compute":
                    sleep_time = 10

        # Send disk stats from all the storage compute nodes
        if self.node_type == "storage-compute":
            self.create_and_send_osd_stats()
            self.create_and_send_disk_stats()
            sleep_time = 6

        time.sleep(sleep_time)
        return

    def runforever(self, test=False):
        # sleep for 6 seconds. There is a sleep in iostat for 4 seconds
        # send pool/disk/osd information to db
        while 1:
            self.send_process_state_db()

def parse_args(args_str):

    # Source any specified config/ini file
    # Turn off help, so we show all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file",
                             help="Specify config file", metavar="FILE")

    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'node_type': 'storage-compute',
        'log_local': True,
        'log_level': 'SYS_NOTICE',
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        #'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
    }
    sandesh_opts = {
        'sandesh_keyfile': '/etc/contrail/ssl/private/server-privkey.pem',
        'sandesh_certfile': '/etc/contrail/ssl/certs/server.pem',
        'sandesh_ca_cert': '/etc/contrail/ssl/certs/ca-cert.pem',
        'sandesh_ssl_enable': False,
        'introspect_ssl_enable': False,
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read([args.conf_file])
        defaults.update(dict(config.items("DEFAULTS")))
        if 'SANDESH' in config.sections():
            sandesh_opts.update(dict(config.items('SANDESH')))
            if 'sandesh_ssl_enable' in config.options('SANDESH'):
                sandesh_opts['sandesh_ssl_enable'] = config.getboolean(
                    'sandesh', 'sandesh_ssl_enable')
            if 'introspect_ssl_enable' in config.options('SANDESH'):
                sandesh_opts['introspect_ssl_enable'] = config.getboolean(
                    'sandesh', 'introspect_ssl_enable')

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    defaults.update(sandesh_opts)
    parser.set_defaults(**defaults)

    parser.add_argument("--log_local", action="store_true",
                        help="Enable local logging of sandesh messages")
    parser.add_argument("--node_type",
                        help="node type of the storage")
    parser.add_argument("--log_level",
                        help="Severity level for local logging of sandesh messages")
    parser.add_argument("--log_category",
                        help="Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
                        help="Sandesh send rate limit in messages/sec")
    parser.add_argument("--sandesh_keyfile",
                        help="Sandesh ssl private key")
    parser.add_argument("--sandesh_certfile",
                        help="Sandesh ssl certificate")
    parser.add_argument("--sandesh_ca_cert",
                        help="Sandesh CA ssl certificate")
    parser.add_argument("--sandesh_ssl_enable", action="store_true",
                        help="Enable ssl for sandesh connection")
    parser.add_argument("--introspect_ssl_enable", action="store_true",
                        help="Enable ssl for introspect connection")

    args = parser.parse_args(remaining_argv)
    return args


def main(args_str=None):

    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)

    prog = EventManager(args.node_type)

    collector_addr = []
    if (args.node_type == 'storage-compute' or args.node_type == 'storage-master'):
        #storage node module initialization part
        module = Module.STORAGE_STATS_MGR
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        instance_id = INSTANCE_ID_DEFAULT
        #if args.sandesh_send_rate_limit is not None:
        #    SandeshSystem.set_sandesh_send_rate_limit( \
        #        args.sandesh_send_rate_limit)
        sandesh_config = SandeshConfig(args.sandesh_keyfile,
            args.sandesh_certfile, args.sandesh_ca_cert,
            args.sandesh_ssl_enable, args.introspect_ssl_enable)
        sandesh_global.init_generator(
            module_name,
            socket.gethostname(),
            node_type_name,
            instance_id,
            collector_addr,
            module_name,
            HttpPortStorageStatsmgr,
            ['stats_daemon.sandesh.storage'],
            config=sandesh_config)

        sandesh_global.set_logging_params(
            enable_local_log=args.log_local,
            category=args.log_category,
            level=args.log_level,
            file=args.log_file, enable_syslog=False,
            syslog_facility='LOG_LOCAL0')
    gevent.joinall([gevent.spawn(prog.runforever)])


if __name__ == '__main__':
    main()
