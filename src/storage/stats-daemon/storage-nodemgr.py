#!/usr/bin/python

doc = """\
Storage Compute manager

Description of Node manager options:
--nodetype: Type of node which nodemgr is managing
--discovery_server: filename as argument 

"""

from gevent import monkey; monkey.patch_all()
import os
import glob
import sys
import socket
import subprocess
import json
import time
import datetime
import pdb
import re

from supervisor import childutils

scriptpath = "/usr/lib/python2.7/site-packages"
sys.path.append(os.path.abspath(scriptpath))
from storage_stats.sandesh.storage.ttypes import *
from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest 
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

    def __init__(self, node_type='contrail-analytics'):
        self.stdin = sys.stdin
        self.stdout = sys.stdout
        self.stderr = sys.stderr
        self.max_cores = 4
        self.max_old_cores = 3
        self.max_new_cores = 1
        self.node_type = node_type
	self._hostname = socket.gethostname()
	pattern = 'rm -rf ceph.conf; ln -s /etc/ceph/ceph.conf ceph.conf'
	subprocess.check_output(pattern, stderr=subprocess.STDOUT, shell=True)
	

    ''' 
    This function reads the ceph rados statistics. 
    Parses this statistics output and gets the read_cnt/read_bytes \
    write_cnt/write_bytes. ComputeStoragePool object created and all \
    the above statictics are assigned
    UVE send call invoked to send the ComputeStoragePool object
    '''
    def  create_and_send_pool_stats(self):
	    res = subprocess.check_output('/usr/bin/rados df', stderr=subprocess.STDOUT, shell=True)
	    arr = res.splitlines()
	    for line in arr:
                if line != arr[0]:
                    result = re.sub('\s+',' ',line).strip()
		    arr1 = result.split()
                    if arr1[0] != "total":
            		cs_pool = ComputeStoragePool()
            		cs_pool.name  = self._hostname + ':' + arr1[0]
	    		pool_stats = PoolStats()
		        pool_stats.pool = arr1[0]
		        pool_stats.reads = int(arr1[7])
		        pool_stats.read_kbytes = int(arr1[8])
		        pool_stats.writes = int(arr1[9])
		        pool_stats.write_kbytes = int(arr1[10])
		        cs_pool.pool_stats = [pool_stats]
	    	        pool_stats_trace = ComputeStoragePoolTrace(data=cs_pool)
            	        sys.stderr.write('sending UVE:' + str(pool_stats_trace))
            	        pool_stats_trace.send()

    ''' 
    This function checks if an osd is active, if yes parses output of \
    osd dump ComputeStorageOsd object created and statictics are assigned
    UVE send call invoked to send the ComputeStorageOsd object
    '''
    def create_and_send_osd_stats(self):
        res = subprocess.check_output('ls /var/lib/ceph/osd', stderr=subprocess.STDOUT, shell=True)
        arr = res.splitlines()
        linecount= 0
        for line in arr:
                cmd = "cat /var/lib/ceph/osd/"+arr[linecount]+"/active"
		is_active = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)  
		if is_active == "ok\n":
        		cs_osd = ComputeStorageOsd()
			cs_osd.current_state = "active"
        		osd_stats = OsdStats()
			# this means osd is not down
			num = arr[linecount].split('-')[1]
			osd_stats.osd_name = "osd."+num
			cmd = "ceph osd dump | grep "+osd_stats.osd_name+" | cut -d \" \" -f22"
			uuid = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)
			osd_stats.uuid = uuid.rstrip("\n")
			ceph_name = "ceph-"+osd_stats.osd_name+".asok"
			# optimized the pattern search. combined multiple search to one regex.
			# In this case subprocess is called once.
			cmd =  "ceph --admin-daemon /var/run/ceph/" + ceph_name + " perf dump | egrep  -w \"\\\"op_w\\\":|\\\"op_r\\\":|\\\"subop_r\\\":|\\\"subop_w\\\":|\\\"op_r_out_bytes\\\":|\\\"subop_r_out_bytes\\\":|\\\"op_w_in_bytes\\\":|\\\"subop_w_in_bytes\\\":\""
        		cs_osd.name = self._hostname + ':' + osd_stats.osd_name
			try:
			    res1 = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)
			    arr1 = res1.splitlines()
			    osd_stats.reads = 0
			    osd_stats.writes = 0
			    osd_stats.read_kbytes = 0
			    osd_stats.write_kbytes = 0
			    for line1 in arr1:		    
                                result = re.sub('\s+',' ',line1).strip()    # replace multiple spaces to single space here
			        line2 = result.split(":")
			        if len(line2) != 0:
				    if line2[0].find('op_r_out_bytes') != -1:
					osd_stats.read_kbytes += int(line2[1].rstrip(",").strip(' '))  
				    elif line2[0].find('subop_r_out_bytes') != -1:
					osd_stats.read_kbytes += int(line2[1].rstrip(",").strip(' '))
				    elif line2[0].find('op_w_in_bytes') != -1:
					osd_stats.write_kbytes += int(line2[1].rstrip(",").strip(' '))
				    elif line2[0].find('subop_w_in_bytes') != -1:
					osd_stats.write_kbytes += int(line2[1].rstrip(",").strip(' '))
				    elif line2[0].find('subop_r') != -1:
					osd_stats.reads += int(line2[1].rstrip(",").strip(' '))
				    elif line2[0].find('op_r') != -1:
				        osd_stats.reads += int(line2[1].rstrip(",").strip(' '))
				    elif line2[0].find('subop_w') != -1:
					osd_stats.writes += int(line2[1].rstrip(",").strip(' '))
				    elif line2[0].find('op_w') != -1:
					osd_stats.writes += int(line2[1].rstrip(",").strip(' '))
			except:				
				pass
                else:
	            cs_osd.current_state = "inactive"
		cs_osd.osd_stats = [osd_stats]
    		osd_stats_trace = ComputeStorageOsdTrace(data=cs_osd)
       	    	sys.stderr.write('sending UVE:' + str(osd_stats_trace))
      		osd_stats_trace.send()
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
    ''' 
    This function parses output of iostat and assigns statistice to \
    ComputeStorageDisk
    UVE send call invoked to send the ComputeStorageDisk object
    '''
    def create_and_send_disk_stats(self):
	#iostat to get the raw disk list
	res = subprocess.check_output('iostat', stderr=subprocess.STDOUT, shell=True)
	disk_list = res.splitlines()
	#osd disk list to get the mapping of osd to raw disk
	pattern = 'ceph-deploy disk list ' + self._hostname 
	res1 = subprocess.check_output(pattern, stderr=subprocess.STDOUT, shell=True)
	osd_list = res1.splitlines() 
	#df used to get the free space of all disks
	res1 = subprocess.check_output('df -h', stderr=subprocess.STDOUT, shell=True)
	df_list = res1.splitlines()
	osd_map = [] 
	for line in osd_list:
	    if line.find('ceph data, active') != -1:
	    	result = re.sub('\s+',' ',line).strip()    # replace multiple spaces to single space here
	    	arr1 = result.split()
		osd_map_obj = osdMap()
		osd_map_obj.osd_disk = arr1[2]
		osd_map_obj.osd = arr1[8]
		osd_map_obj.journal = arr1[10]
		osd_map.append(osd_map_obj)

	disk_usage = []
	for line in df_list:
	    if line.find('sda') != -1:
	    	result = re.sub('\s+',' ',line).strip()    # replace multiple spaces to single space here
	    	arr1 = result.split()
		if arr1[5] == '/':
		    disk_usage_obj = diskUsage()
		    disk_usage_obj.disk = arr1[0]
		    disk_usage_obj.disk_size = arr1[1]
		    disk_usage_obj.disk_used = arr1[2]
		    disk_usage_obj.disk_avail = arr1[3]
		    disk_usage.append(disk_usage_obj);
		
	    elif line.find('sd') != -1:
	    	result = re.sub('\s+',' ',line).strip()    # replace multiple spaces to single space here
	    	arr1 = result.split()
		disk_usage_obj = diskUsage()
		disk_usage_obj.disk = arr1[0]
		disk_usage_obj.disk_size = arr1[1]
		disk_usage_obj.disk_used = arr1[2]
		disk_usage_obj.disk_avail = arr1[3]
		disk_usage.append(disk_usage_obj);
	
        # create a dictionary of disk_name: model_num + serial_num
        new_dict = dict()
        resp = subprocess.check_output('ls -l /dev/disk/by-id/', stderr=subprocess.STDOUT, shell=True)
        arr_disks = resp.splitlines()
        for line in arr_disks[1:]:
            resp1 = line.split()
            if resp1[-1].find('sd') != -1 and resp1[8].find('part') == -1 and resp1[8].find('ata') != -1:
                new_dict[resp1[-1].split('/')[2]] = resp1[8]
         
        cs_disk1 = ComputeStorageDisk()
        cs_disk1.list_of_curr_disks = []
	for line in disk_list:       # this will have all rows 
            result = re.sub('\s+',' ',line).strip()    # replace multiple spaces to single space here
            arr1 = result.split()  
            if len(arr1) != 0 and arr1[0].find('sd') != -1: 
                cs_disk = ComputeStorageDisk()
                cs_disk.name  = self._hostname + ':' + arr1[0]
                cs_disk1.list_of_curr_disks.append(arr1[0])
		cs_disk.is_osd_disk = self.find_osdmaplist(osd_map, arr1[0])
		disk_usage_obj = self.find_diskusagelist(disk_usage, arr1[0])
		if disk_usage_obj is None:
		    cs_disk.current_disk_usage = 0
                    
	        else:
		    last = disk_usage_obj.disk_used[-1:]
		    if last == 'K':
			cs_disk.current_disk_usage = long(float (disk_usage_obj.disk_used.strip('K')) * 1024)
		    elif last == 'M':
			cs_disk.current_disk_usage = long(float (disk_usage_obj.disk_used.strip('M')) * 1024 * 1024)
		    elif last == 'G':
			cs_disk.current_disk_usage = long(float (disk_usage_obj.disk_used.strip('G')) * 1024 * 1024 * 1024)
	            elif last == 'T':
			cs_disk.current_disk_usage = long(float (disk_usage_obj.disk_used.strip('T')) * 1024 * 1024 * 1024 * 1024)
                    elif last == 'P':
			cs_disk.current_disk_usage = long(float (disk_usage_obj.disk_used.strip('P')) * 1024 * 1024 * 1024 * 1024 * 1024)
		
	        disk_stats = DiskStats()
		disk_stats.disk_name = arr1[0]
                if new_dict.has_key(disk_stats.disk_name):
                    disk_stats.uuid = new_dict.get(disk_stats.disk_name)
		disk_stats.iops = int(float(arr1[1]))
		disk_stats.bw = int(float(arr1[2])) + int(float(arr1[3]))
		cmd = "cat /sys/block/"+disk_stats.disk_name+"/stat"
	        res = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)
	   	arr = re.sub('\s+',' ',res).strip().split()
		disk_stats.reads = int(arr[0])
		disk_stats.writes = int(arr[4])
		disk_stats.read_kbytes = int(arr[2])
		disk_stats.write_kbytes = int(arr[6]) 
		cs_disk.disk_stats = [disk_stats]
	    	disk_stats_trace = ComputeStorageDiskTrace(data=cs_disk)
       	    	sys.stderr.write('sending UVE:' + str(disk_stats_trace))
            	disk_stats_trace.send()
        
        cs_disk1_trace = ComputeStorageDiskTrace(data=cs_disk1)
        sys.stderr.write('sending UVE:' +str(cs_disk1_trace))
        if len(set(cs_disk1.list_of_curr_disks).difference(set(self.prev_list))) != 0:
            cs_disk1_trace.send()
        self.prev_list=cs_disk1.list_of_curr_disks

   # send UVE for updated process state database
    def send_process_state_db(self, sandeshconn):
        # send UVE based on storage-compute node type
        if (self.node_type == 'storage-compute'):
            self.create_and_send_pool_stats()
	    self.create_and_send_osd_stats()
	    self.create_and_send_disk_stats()

    
    def runforever(self, sandeshconn, test=False):
    #sys.stderr.write(str(self.rules_data['Rules'])+'\n')
        prev_current_time = int(time.time())    
        while 1:
            gevent.sleep(1)
	    self.headers['eventname'] = 'TICK_60'

            # do periodic events
            if self.headers['eventname'].startswith("TICK_60"):
                # check for openstack nova compute status

                current_time = int(time.time())
                if ((abs(current_time - prev_current_time)) > 5):
                    if (self.node_type == "storage-compute"):
                    	self.send_process_state_db(sandeshconn)
                    prev_current_time = int(time.time())
            childutils.listener.ok(self.stdout)

def main(argv=sys.argv):
# Parse Arguments
    import argparse
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument("--rules",
			default = '',
                        help = 'Rules file to use for processing events')
    parser.add_argument("--nodetype",
                        default = 'contrail-analytics',
                        help = 'Type of node which nodemgr is managing')
    parser.add_argument("--discovery_server",
                        default = '',
                        help = 'IP address file')
    parser.add_argument("--collectors",
                        default = '',
                        help = 'Collector addresses in format ip1:port1 ip2:port2')
    parser.add_argument("--discovery_port",
                        type = int,
                        default = 5998,
                        help = 'Port of Discovery Server')
    try:
        _args = parser.parse_args()
    except:
        usage()
    node_type = _args.nodetype

    file1 = open(_args.discovery_server,'r')
    for line in file1:
        if len(line) !=0 and line.find('DISCOVERY') != -1:
            arr = line.split('=')
            ip  = arr[1].rstrip()              
    node_type = _args.nodetype
    discovery_port = _args.discovery_port
    sys.stderr.write("Discovery port: " + str(discovery_port) + "\n")
    if _args.collectors is "":
        collector_addr = []
    else:
        collector_addr = _args.collectors.split()
    sys.stderr.write("Collector address: " + str(collector_addr) + "\n")
    prog = EventManager(node_type)

    if (node_type == 'storage-compute'):
        try:
            import discovery.client as client
        except:
            import discoveryclient.client as client

        # since this may be a local node, wait for sometime to let 
	# collector come up
        import time
        module = Module.STORAGE_NODE_MGR
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        instance_id = INSTANCE_ID_DEFAULT
        _disc= client.DiscoveryClient(ip , discovery_port, module_name)
        sandesh_global.init_generator(module_name, socket.gethostname(), 
            node_type_name, instance_id, collector_addr, module_name, 
            8103, ['storage_stats.sandesh.storage'], _disc)
    gevent.joinall([gevent.spawn(prog.runforever, sandesh_global)])

if __name__ == '__main__':
    main()
