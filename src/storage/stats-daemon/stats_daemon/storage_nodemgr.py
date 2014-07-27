#!/usr/bin/python

doc = """\
Storage Compute manager


"""
import sys
from gevent import monkey
monkey.patch_all(thread=not 'unittest' in sys.modules)
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

from stats_daemon.sandesh.storage.ttypes import *
from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType, INSTANCE_ID_DEFAULT
from subprocess import Popen, PIPE


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
        self.units = self.init_units()
        self.curr_read_kbytes = 0
        self.curr_write_kbytes = 0
        self.curr_reads = 0
        self.curr_writes = 0
        self.curr_read_latency = 0
        self.curr_write_latency = 0
        pattern = 'rm -rf ceph.conf; ln -s /etc/ceph/ceph.conf ceph.conf'
        self.call_subprocess(pattern)

    def init_units(self):
        units = dict();
        units['K'] = 1024
        units['M'] = int(units['K']) * 1024
        units['G'] = int(units['M']) * 1024
        units['T'] = int(units['G']) * 1024
        units['P'] = int(units['T']) * 1024
        return units

    '''
    This function reads the ceph rados statistics.
    Parses this statistics output and gets the read_cnt/read_bytes \
    write_cnt/write_bytes. ComputeStoragePool object created and all \
    the above statictics are assigned
    UVE send call invoked to send the ComputeStoragePool object
    '''

    def call_subprocess(self, cmd):
        res = subprocess.check_output(
                  cmd, stderr=subprocess.STDOUT, shell=True)
        return res

    def call_send(self, send_inst):
        send_inst.send()

    def create_and_send_pool_stats(self):
        res = self.call_subprocess('/usr/bin/rados df')
        arr = res.splitlines()
        for line in arr:
            if line != arr[0]:
                result = re.sub(
                    '\s+', ' ', line).strip()
                arr1 = result.split()
                if arr1[0] != "total":
                    cs_pool = ComputeStoragePool()
                    cs_pool.name = self._hostname + ':' + arr1[0]
                    pool_stats = PoolStats()
                    pool_stats.reads = int(arr1[7])
                    pool_stats.read_kbytes = int(arr1[8])
                    pool_stats.writes = int(arr1[9])
                    pool_stats.write_kbytes = int(arr1[10])
                    cs_pool.info_stats = [pool_stats]
                    pool_stats_trace = ComputeStoragePoolTrace(
                        data=cs_pool)
                    self.call_send(pool_stats_trace)



    def populate_osd_total_stats(self, osdname, osd_stats):
        ceph_name = "ceph-" + osdname + ".asok"
        cmd = "ceph --admin-daemon /var/run/ceph/" + ceph_name + \
              " perf dump | egrep  -w \"\\\"op_w\\\":|\\\"" + \
              "op_r\\\":|\\\"subop_r\\\":|\\\"subop_w\\\":|\\\"" + \
              "op_r_out_bytes\\\":|\\\"subop_r_out_bytes\\\":|" + \
              "\\\"op_w_in_bytes\\\":|\\\"subop_w_in_bytes\\\":\""
        try:
            res1 = self.call_subprocess(cmd)
            arr1 = res1.splitlines()
            for line1 in arr1:
                result = re.sub(
                             '\s+', ' ', line1).strip()
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

    def diff_read_kbytes(self, line, osd_stats, temp_osd_stats,
                         osd_prev_stats):
        self.curr_read_kbytes += int(
            line.rstrip(",").strip(' ')) / 1024
        temp_osd_stats.read_kbytes = self.curr_read_kbytes
        osd_stats.read_kbytes = self.curr_read_kbytes - \
            osd_prev_stats.read_kbytes

    def diff_write_kbytes(self, line, osd_stats, temp_osd_stats,
                          osd_prev_stats):
        self.curr_write_kbytes += int(
            line.rstrip(",").strip(' ')) / 1024
        temp_osd_stats.write_kbytes = self.curr_write_kbytes
        osd_stats.write_kbytes = self.curr_write_kbytes - \
            osd_prev_stats.write_kbytes

    def diff_read_cnt(self, line, osd_stats, temp_osd_stats,
                      osd_prev_stats):
        self.curr_reads += int(
            line.rstrip(",").strip(' '))
        temp_osd_stats.reads = self.curr_reads
        osd_stats.reads = self.curr_reads - \
            osd_prev_stats.reads

    def diff_write_cnt(self, line, osd_stats, temp_osd_stats,
                       osd_prev_stats):
        self.curr_writes += int(
            line.rstrip(",").strip(' '))
        temp_osd_stats.writes = self.curr_writes
        osd_stats.writes = self.curr_writes - \
            osd_prev_stats.writes


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
            arr1 = res1.splitlines()
            for line1 in arr1:
                result = re.sub(
                    '\s+', ' ', line1).strip()
                line2 = result.split(":")
                if len(line2) != 0:
                    if line2[0].find('subop_r_out_bytes') != -1 or \
                        line2[0].find('op_r_out_bytes') != -1:
                        self.diff_read_kbytes(line2[1],
                            osd_stats, temp_osd_stats, osd_prev_stats)
                    elif line2[0].find('subop_w_in_bytes') != -1 or \
                        line2[0].find('op_w_in_bytes') != -1:
                        self.diff_write_kbytes(line2[1],
                            osd_stats, temp_osd_stats, osd_prev_stats)
                    elif line2[0].find('subop_r') != -1 or \
                        line2[0].find('op_r') != -1:
                        self.diff_read_cnt(line2[1],
                            osd_stats, temp_osd_stats, osd_prev_stats)
                    elif line2[0].find('subop_w') != -1 or \
                        line2[0].find('op_w') != -1:
                        self.diff_write_cnt(line2[1],
                            osd_stats, temp_osd_stats, osd_prev_stats)
        except:
            pass


    def compute_read_latency(self, arr, line, index, osd_stats):
        avgcount = int(line.rstrip(",").strip(' '))
        sum_rlatency = int(
            float(arr[index + 1].split(":")[1].strip().rstrip("},")))
        if avgcount != 0:
            osd_stats.op_r_latency += (sum_rlatency * 1000) / avgcount
        else:
            osd_stats.op_r_latency += 0

    def compute_write_latency(self, arr, line, index, osd_stats):
        avgcount = int(line.rstrip(",").strip(' '))
        sum_wlatency = int(
            float(arr[index + 1].split(":")[1].strip().rstrip("},")))
        if avgcount != 0:
            osd_stats.op_w_latency += (sum_wlatency * 1000) / avgcount
        else:
            osd_stats.op_w_latency += 0

    def populate_osd_latency_stats(self, osdname, osd_stats):
       ceph_name = "ceph-" + \
           osdname + ".asok"
       cmd2 = "ceph --admin-daemon /var/run/ceph/" + ceph_name + \
           " perf dump | egrep -A 1 -w \"\\\"" +\
           "op_r_latency\\\":|\\\"subop_r_latency\\\":|" + \
           "\\\"op_w_latency\\\":|\\\"" + \
           "subop_w_latency\\\":\""
       try:
           res2 = self.call_subprocess(cmd2)
           arr2 = res2.splitlines()
           for index in range(len(arr2)):
           # replace multiple spaces
           # to single space here
               result = re.sub(
                   '\s+', ' ', arr2[index]).strip()
               line2 = result.split(":")
               if len(line2) != 0:
                   if line2[0].find('subop_r_latency') != -1 or \
                       line2[0].find('op_r_latency') != -1:
                       self.compute_read_latency(arr2,
                           line2[2], index, osd_stats)
                   elif line2[0].find('subop_w_latency') != -1 or \
                       line2[0].find('op_w_latency') != -1:
                       self.compute_write_latency(arr2,
                           line2[2], index, osd_stats)
       except:
           pass


    '''
    This function checks if an osd is active, if yes parses output of \
    osd dump ComputeStorageOsd object created and statictics are assigned
    UVE send call invoked to send the ComputeStorageOsd object
    '''

    def create_and_send_osd_stats(self):
        res = self.call_subprocess('ls /var/lib/ceph/osd')
        arr = res.splitlines()
        linecount = 0
        for line in arr:
            no_prev_osd = 0
            cmd = "cat /var/lib/ceph/osd/" + \
                arr[linecount] + "/active"
            is_active = self.call_subprocess(cmd)
            #instantiate osd and its state
            cs_osd = ComputeStorageOsd()
            cs_osd_state = ComputeStorageOsdState()
            osd_stats = OsdStats()
            temp_osd_stats = OsdStats()
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
                osd_name = "osd." + num
                cmd = "ceph osd dump | grep " + \
                    osd_name + " | cut -d \" \" -f22"
                uuid = self.call_subprocess(cmd)
                cs_osd.uuid = uuid.rstrip("\n")
                osd_prev_stats = self.dict_of_osds.get(
                    cs_osd.uuid)
                cs_osd.name = self._hostname + \
                    ':' + osd_name
                if osd_prev_stats is None:
                    no_prev_osd = 1
                    self.populate_osd_total_stats(osd_name, osd_stats)
                else:
                    self.populate_osd_diff_stats(osd_name, osd_stats,
                                                 temp_osd_stats,
                                                 osd_prev_stats)
                    self.populate_osd_latency_stats(osd_name, osd_stats)
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
            else:
                self.dict_of_osds[
                    cs_osd.uuid] = osd_stats
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
        res = self.call_subprocess('iostat')
        disk_list = res.splitlines()
        # osd disk list to get the mapping of osd to
        # raw disk
        pattern = 'ceph-deploy disk list ' + \
            self._hostname
        res1 = self.call_subprocess(pattern)
        osd_list = res1.splitlines()
        # df used to get the free space of all disks
        res1 = self.call_subprocess('df -h')
        df_list = res1.splitlines()
        osd_map = []
        for line in osd_list:
            if line.find('ceph data, active') != -1:
                # replace multiple spaces to single
                # space here
                result = re.sub(
                    '\s+', ' ', line).strip()
                arr1 = result.split()
                osd_map_obj = osdMap()
                osd_map_obj.osd_disk = arr1[2]
                osd_map_obj.osd = arr1[8]
                if len(arr1) > 11:
                    osd_map_obj.journal = arr1[10]
                osd_map.append(osd_map_obj)

        disk_usage = []
        for line in df_list:
            if line.find('sda') != -1:
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

                elif line.find('sd') != -1:
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

        # create a dictionary of disk_name: model_num +
        # serial_num
        new_dict = dict()
        resp = self.call_subprocess('ls -l /dev/disk/by-id/')
        arr_disks = resp.splitlines()
        for line in arr_disks[1:]:
            resp1 = line.split()
            if (resp1[-1].find('sd') != -1 and
                resp1[8].find('part') == -1 and
                    resp1[8].find('ata') != -1):
                    new_dict[resp1[-1].split('/')[2]] = resp1[8]

        cs_disk1 = ComputeStorageDisk()
        cs_disk1.list_of_curr_disks = []
        for line in disk_list:       # this will have all rows
            # replace multiple spaces to single space here
            result = re.sub('\s+', ' ', line).strip()
            arr1 = result.split()
            if len(arr1) != 0 and arr1[0].find('sd') != -1:
                cs_disk = ComputeStorageDisk()
                cs_disk.name = self._hostname + ':' + arr1[0]
                cs_disk1.list_of_curr_disks.append(arr1[0])
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
                if arr1[0] in new_dict:
                    cs_disk.uuid = new_dict.get(arr1[0])
                disk_stats.iops = int(float(arr1[1]))
                disk_stats.bw = int(float(arr1[2])) + \
                    int(float(arr1[3]))
                cmd = "cat /sys/block/"+ arr1[0] +"/stat"
                res = self.call_subprocess(cmd)
                arr = re.sub('\s+', ' ', res).strip().split()
                disk_stats.reads = int(arr[0])
                disk_stats.writes = int(arr[4])
                disk_stats.read_kbytes = int(arr[2])
                disk_stats.write_kbytes = int(arr[6])
                cs_disk.info_stats = [disk_stats]
                disk_stats_trace = ComputeStorageDiskTrace(data=cs_disk)
                self.call_send(disk_stats_trace)

        cs_disk1_trace = ComputeStorageDiskTrace(data=cs_disk1)
        # sys.stderr.write('sending UVE:' +str(cs_disk1_trace))
        if len(set(cs_disk1.list_of_curr_disks).
               difference(set(self.prev_list))) != 0:
            self.call_send(cs_disk1_trace)
        self.prev_list = []
        for i in xrange(0, len(cs_disk1.list_of_curr_disks)-1):
            self.prev_list.append(cs_disk1.list_of_curr_disks[i])

    # send UVE for updated process state database
    def send_process_state_db(self):
        self.create_and_send_pool_stats()
        self.create_and_send_osd_stats()
        self.create_and_send_disk_stats()

    def runforever(self, sandeshconn, test=False):
        # sleep for 5 seconds
        # send pool/disk/osd information to db
        while 1:
            gevent.sleep(5)
            if (self.node_type == "storage-compute"):
                self.send_process_state_db()

def parse_args(args_str):

    # Source any specified config/ini file
    # Turn off help, so we show all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file",
                             help="Specify config file", metavar="FILE")

    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'disc_server_ip': '127.0.0.1',
        'disc_server_port': 5998,
        'node_type': 'storage-compute'
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read([args.conf_file])
        defaults.update(dict(config.items("DEFAULTS")))

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

    parser.set_defaults(**defaults)

    parser.add_argument("--disc_server_ip",
                        help="IP address of the discovery server")
    parser.add_argument("--disc_server_port",
                        help="Port of the discovery server")
    parser.add_argument("--node_type",
                        help="node type of the storage")

    args = parser.parse_args(remaining_argv)
    return args


def main(args_str=None):

    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)

    # dump the read values
    sys.stderr.write("node_type: " + args.node_type + "\n")
    sys.stderr.write("Discovery ip: " + args.disc_server_ip + "\n")
    sys.stderr.write("Discovery port: " + str(args.disc_server_port) + "\n")

    # create event manager
    prog = EventManager(args.node_type)

    collector_addr = []
    if (args.node_type == 'storage-compute'):
        try:
            import discovery.client as client
        except:
            import discoveryclient.client as client

        #storage node module initialization part
        module = Module.STORAGE_STATS_MGR
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        instance_id = INSTANCE_ID_DEFAULT
        _disc = client.DiscoveryClient(args.disc_server_ip,
                                       args.disc_server_port,
                                       module_name)
        sandesh_global.init_generator(
            module_name,
            socket.gethostname(),
            node_type_name,
            instance_id,
            collector_addr,
            module_name,
            8103,
            ['stats_daemon.sandesh.storage'],
            _disc)
    gevent.joinall([gevent.spawn(prog.runforever, sandesh_global)])

if __name__ == '__main__':
    main()
