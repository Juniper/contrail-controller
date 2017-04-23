#!/usr/bin/python
import platform
import os
import sys, select
import subprocess
import time
import math
from distutils.version import LooseVersion

class SetupCephUtils(object):

    global POOL_CRUSH_MAP
    POOL_CRUSH_MAP = '/tmp/ma-crush-map-pool'
    global POOL_CRUSH_MAP_TXT
    POOL_CRUSH_MAP_TXT = '/tmp/ma-crush-map-pool.txt'
    global POOL_CRUSH_MAP_MOD
    POOL_CRUSH_MAP_MOD = '/tmp/ma-crush-map-pool-mod'
    global POOL_CRUSH_MAP_MOD_TXT
    POOL_CRUSH_MAP_MOD_TXT = '/tmp/ma-crush-map-pool-mod.txt'
    global INIT_CRUSH_MAP
    INIT_CRUSH_MAP = '/tmp/ma-crush-map-init'
    global INIT_CRUSH_MAP_MOD
    INIT_CRUSH_MAP_MOD = '/tmp/ma-crush-map-init-mod'
    global INIT_CRUSH_MAP_TXT
    INIT_CRUSH_MAP_TXT = '/tmp/ma-crush-map-init.txt'
    global INIT_CRUSH_MAP_MOD_TXT
    INIT_CRUSH_MAP_MOD_TXT = '/tmp/ma-crush-map-init-mod.txt'
    global CS_CRUSH_MAP
    CS_CRUSH_MAP = '/tmp/ma-crush-map-cs'
    global CS_CRUSH_MAP_MOD
    CS_CRUSH_MAP_MOD = '/tmp/ma-crush-map-cs-mod'
    global CS_CRUSH_MAP_TXT
    CS_CRUSH_MAP_TXT = '/tmp/ma-crush-map-cs.txt'
    global CS_CRUSH_MAP_MOD_TXT
    CS_CRUSH_MAP_MOD_TXT = '/tmp/ma-crush-map-cs-mod.txt'
    global CS_CRUSH_MAP_MOD_TMP_TXT
    CS_CRUSH_MAP_MOD_TMP_TXT = '/tmp/ma-crush-map-cs-mod-tmp.txt'
    global CEPH_ADMIN_KEYRING
    CEPH_ADMIN_KEYRING = '/etc/ceph/ceph.client.admin.keyring'
    global RADOS_KEYRING
    RADOS_KEYRING = '/etc/ceph/ceph.client.radosgw.keyring'
    global CINDER_PATCH_FILE
    CINDER_PATCH_FILE = '/tmp/manager.patch'
    global CINDER_VOLUME_MGR_PY
    CINDER_VOLUME_MGR_PY = '/usr/lib/python2.7/dist-packages/cinder/volume/manager.py'
    global CEPH_DEPLOY_PATCH_FILE
    CEPH_DEPLOY_PATCH_FILE = '/tmp/ceph_deploy.patch'
    global ETC_CEPH_CONF
    ETC_CEPH_CONF = '/etc/ceph/ceph.conf'
    global RADOS_GW_LOG_FILE
    RADOS_GW_LOG_FILE = '/var/log/radosgw/client.radosgw.gateway.log'
    global RADOS_GW_FRONT_END
    RADOS_GW_FRONT_END = 'fastcgi socket_port=9000 socket_host=0.0.0.0'
    global RADOS_GW_SOCKET_PATH
    RADOS_GW_SOCKET_PATH =  '/var/run/ceph/ceph.radosgw.gateway.fastcgi.sock'
    global LIB_RADOS_GW
    LIB_RADOS_GW = '/var/lib/ceph/radosgw/ceph-radosgw.gateway'
    global APACHE_RGW_CONF
    APACHE_RGW_CONF = '/etc/apache2/conf-available/rgw.conf'
    global OBJECT_STORAGE_USER_FILE
    OBJECT_STORAGE_USER_FILE = '/etc/contrail/object_storage_swift_s3_auth.txt'
    global TRUE
    TRUE = 1
    global FALSE
    FALSE = 0
    # Maximum number of pool that can be created for HDD and SSD
    global MAX_POOL_COUNT
    MAX_POOL_COUNT = 1024
    global REPLICA_ONE
    REPLICA_ONE = 1
    global REPLICA_TWO
    REPLICA_TWO = 2
    global REPLICA_DEFAULT
    REPLICA_DEFAULT = 2
    # Host HDD/SSD dictionary/counters,
    # populated during HDD/SSD pool configuration
    global host_hdd_dict
    host_hdd_dict = {}
    global host_ssd_dict
    host_ssd_dict = {}
    global hdd_pool_count
    hdd_pool_count = 0
    global ssd_pool_count
    ssd_pool_count = 0
    # Chassis ruleset for each pool,
    # populated during chassis configuration
    # Used during pool configuration
    global chassis_hdd_ruleset
    chassis_hdd_ruleset = 0
    global chassis_ssd_ruleset
    chassis_ssd_ruleset = 0
    # Crush id used during crush map changes
    global crush_id
    # HDD/SSD pool list, populated during HDD/SSD pool configuration
    # Used during pool, virsh, pg/pgp count configurations
    global ceph_pool_list
    ceph_pool_list = []
    global ceph_tier_list
    ceph_tier_list = []
    global ceph_object_store_pools
    ceph_object_store_pools = ['.rgw.root',
                                '.rgw.control',
                                '.rgw.gc',
                                '.rgw.buckets',
                                '.rgw.buckets.index',
                                '.rgw.buckets.extra',
                                '.log',
                                '.intent-log',
                                '.usage',
                                '.users',
                                '.users.email',
                                '.users.swift',
                                '.users.uid',
                                '.rgw',
                                'default.rgw.control',
                                'default.rgw.data.root',
                                'default.rgw.gc',
                                'default.rgw.log',
                                'default.rgw.users.uid',
                                'default.rgw.users.keys',
                                'default.rgw.meta',
                                'default.rgw.users.swift']

    # Function to check if Chassis configuration is disabled or not
    # Returns False if enabled
    # Returns True if disabled
    def is_chassis_disabled(self, chassis_config):
        if chassis_config[0] == 'none':
            return TRUE
        else:
            return FALSE
    #end is_chassis_disabled()

    # Function to check if multipool is disabled or not
    # Returns False if enabled
    # Returns True if disabled
    # Checks for 'P' (for Pool) entry in the disk list in
    # the 2nd or 3rd field.
    def is_multi_pool_disabled(self, storage_disk_config,
                                storage_ssd_disk_config):
        for disks in storage_disk_config:
            journal_available = disks.count(':')
            disksplit = disks.split(':')
            diskcount = disks.count(':')
            if diskcount == 3:
                if disksplit[3][0] == 'P':
                    return FALSE
            elif diskcount == 2:
                if disksplit[2][0] == 'P':
                    return FALSE
        for disks in storage_ssd_disk_config:
            journal_available = disks.count(':')
            disksplit = disks.split(':')
            diskcount = disks.count(':')
            if diskcount == 3:
                if disksplit[3][0] == 'P':
                    return FALSE
            elif diskcount == 2:
                if disksplit[2][0] == 'P':
                    return FALSE
        return TRUE
    #end is_multi_pool_disabled()

    # Function to check if SSD pool is disabled or not
    # Returns False if enabled
    # Returns True if disabled
    def is_ssd_pool_disabled(self, storage_ssd_disk_config):
        if storage_ssd_disk_config[0] == 'none':
            return TRUE
        else:
            return FALSE
    #end is_ssd_pool_disabled()

    def exec_locals(self, arg):
        ret = subprocess.Popen('%s' %(arg), shell=True,
                                stdout=subprocess.PIPE).stdout.read()
        ret = ret[:-1]
        return ret
    #end exec_locals()

    def exec_local(self, arg):
        ret = subprocess.Popen('echo \"[localhost] local: %s\" 1>&2' %(arg), shell=True,
                                stdout=subprocess.PIPE).stdout.read()
        ret = subprocess.Popen('%s' %(arg), shell=True,
                                stdout=subprocess.PIPE).stdout.read()
        ret = ret[:-1]
        return ret
    #end exec_local()

    # Function to set the PG count
    # Verify whether the PGs are in creating state,
    # else set the pg count to the new value
    def set_pg_count_increment(self, pool, pg_count):
        while True:
            time.sleep(2);
            creating_pgs = self.exec_local('sudo ceph -s | grep creating | wc -l')
            if creating_pgs == '0':
                break;
            print 'Waiting for create pgs to complete'
        self.exec_local('sudo ceph -k %s osd pool set %s pg_num %d'
                                    %(CEPH_ADMIN_KEYRING, pool, pg_count))
    #end set_pg_count_increment()

    # Function to set the PGP count
    # Verify whether the PGs are in creating state,
    # else set the pgp count to the new value
    def set_pgp_count_increment(self, pool, pg_count):
        while True:
            time.sleep(2);
            creating_pgs = self.exec_local('sudo ceph -s | grep creating | wc -l')
            if creating_pgs == '0':
                break;
            print 'Waiting for create pgs to complete'
        self.exec_local('sudo ceph -k %s osd pool set %s pgp_num %d'
                                    %(CEPH_ADMIN_KEYRING, pool, pg_count))
    #end set_pgp_count_increment()

    # Function to return nearest power of 2
    # Recommended pg count to be a power of 2
    def next_greater_power_of_2(self, x):
        power = round(math.log(x,2))
        return 2**power
    #end next_greater_power_of_2

    # First level Function to set the PG/PGP count
    def set_pg_pgp_count(self, osd_num, pool, host_cnt):

        # Calculate/Set PG count
        # The pg/pgp set will not take into effect if ceph is already in the
        # process of creating pgs. So its required to do ceph -s and check
        # if the pgs are currently creating and if not set the values

        # Set the num of pgs to 30 times the OSD count. This is based on
        # Firefly release recomendation.

        # Algorithm: PGs can be set to a incremental value of 30 times the
        # current count. Set the value in increments matching 30 times the
        # current value. Do this untill the value matches the required value
        # of 30 times the OSD count.
        rep_size = int(self.exec_local('sudo ceph osd pool get %s size | \
                                    awk \'{print $2}\'' %(pool)))
        while True:
            time.sleep(5);
            creating_pgs = self.exec_local('sudo ceph -s | grep creating | wc -l')
            if creating_pgs == '0':
                break;
            print 'Waiting for create pgs to complete'

        cur_pg = self.exec_local('sudo ceph -k %s osd pool get %s pg_num'
                                    %(CEPH_ADMIN_KEYRING, pool))
        cur_pg_cnt = int(cur_pg.split(':')[1])
        max_pg_cnt = self.next_greater_power_of_2((100 * osd_num)/rep_size)
        if cur_pg_cnt >= max_pg_cnt:
            return
        while True:
            cur_pg = self.exec_local('sudo ceph -k %s osd pool get %s pg_num'
                                    %(CEPH_ADMIN_KEYRING, pool))
            cur_pg_cnt = int(cur_pg.split(':')[1])
            new_pg_cnt = 32 * cur_pg_cnt
            if cur_pg_cnt < (32 * osd_num):
                if new_pg_cnt > (32 * osd_num):
                    new_pg_cnt = 32 * osd_num
            if new_pg_cnt > max_pg_cnt:
                self.set_pg_count_increment(pool, max_pg_cnt)
                break;
            else:
                self.set_pg_count_increment(pool, new_pg_cnt)

        # Set pgp count
        while True:
            time.sleep(5);
            creating_pgs = self.exec_local('sudo ceph -s | grep creating | wc -l')
            if creating_pgs == '0':
                break;
            print 'Waiting for create pgs to complete'

        cur_pg = self.exec_local('sudo ceph -k %s osd pool get %s pgp_num'
                                    %(CEPH_ADMIN_KEYRING, pool))
        cur_pg_cnt = int(cur_pg.split(':')[1])
        max_pg_cnt = self.next_greater_power_of_2((100 * osd_num)/rep_size)
        if cur_pg_cnt >= max_pg_cnt:
            return
        while True:
            cur_pg = self.exec_local('sudo ceph -k %s osd pool get %s pgp_num'
                                    %(CEPH_ADMIN_KEYRING, pool))
            cur_pg_cnt = int(cur_pg.split(':')[1])
            new_pg_cnt = 32 * cur_pg_cnt
            if cur_pg_cnt < (32 * osd_num):
                if new_pg_cnt > (32 * osd_num):
                    new_pg_cnt = 32 * osd_num
            if new_pg_cnt > max_pg_cnt:
                self.set_pgp_count_increment(pool, max_pg_cnt)
                break;
            else:
                self.set_pgp_count_increment(pool, new_pg_cnt)

    #end set_pg_pgp_count()

    # Initialize Crush map to the Original state
    # The crush map is initialized to the original state
    # for further processing with multi-pool and chassis configurations
    # This is done maintain the crush ids across multiple runs of the
    # configuration.
    # The function will get each line from 0 and writes to a new file
    # INIT_CRUSH_MAP_MOD_TXT. All the host entries untill the "root default"
    # entry are written to the new file and the new cursh is returned.
    # The crush ids for each entry is re-initialized starting from 1 which
    # is set for the "root default"
    # Return value: modified crush.
    # Note: This function doesnot apply the crush map
    def initialize_crush(self):
        global crush_id

        self.exec_local('sudo ceph osd getcrushmap -o %s' %(INIT_CRUSH_MAP))
        self.exec_local('sudo crushtool -d %s -o %s'
                                    %(INIT_CRUSH_MAP, INIT_CRUSH_MAP_TXT))
        # Reinitialize ids to avoid duplicates and unused
        root_def_id = 1
        crush_id = root_def_id + 1
        default_reached = 0
        line_num = 0
        self.exec_local('echo "# Start" > %s' %(INIT_CRUSH_MAP_MOD_TXT))
        while True:
            # Get each line from the existing crush map
            item_line = self.exec_local('cat %s | tail -n +%d | head -n 1'
                                %(INIT_CRUSH_MAP_TXT, line_num))
            # Check if "root default" is reached.
            if item_line.find('root default') != -1:
                default_reached = 1
                self.exec_local('echo %s >> %s' %(item_line, INIT_CRUSH_MAP_MOD_TXT))
            # If the end '}' of "root default" is found, the new map can be
            # returned
            elif item_line.find('}') != -1:
                self.exec_local('echo %s >> %s' %(item_line, INIT_CRUSH_MAP_MOD_TXT))
                if default_reached == 1:
                    break
            # Reinitialize the ids starting from 1. Use 1 for the "root default"
            elif item_line.find('id -') != -1:
                if default_reached == 1:
                    self.exec_local('echo "   id -%d" >> %s' %(root_def_id,
                                                        INIT_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('echo "   id -%d" >> %s' %(crush_id,
                                                        INIT_CRUSH_MAP_MOD_TXT))
                    crush_id += 1
            else:
                self.exec_local('echo %s >> %s' %(item_line, INIT_CRUSH_MAP_MOD_TXT))

            line_num += 1
        # Compile the text file and return the crush map.
        # This is done so that the intermediate text map is stored for debug
        self.exec_local('sudo crushtool -c %s -o %s' %(INIT_CRUSH_MAP_MOD_TXT,
                                                        INIT_CRUSH_MAP_MOD))
        return INIT_CRUSH_MAP_MOD
    #end initialize_crush

    # Function to apply the crush map after all the modifications
    def apply_crush(self, input_crush):
        # Apply crush map and return
        self.exec_local('sudo ceph -k %s osd setcrushmap -i %s' %(CEPH_ADMIN_KEYRING,
                                                                input_crush))
        return
    #end apply_crush

    # Crush map modification for Chassis support
    # This ensures that the replica will not happen between nodes
    # in a single chassis.
    # The Chassis id given in the testbed.py for each host will
    # be used to create virtual groups. Replica will not happen between hosts
    # of the same chassis id.
    # For eg., consider a Quanta system that has 4 nodes in a single chassis.
    # All the nodes in a single chassis should be given the same chassis id.
    # Based on the chassis id, a hdd-chassis-<chassis-id> entry will be
    # created. The hdd-chassis-<x> will have the list hosts that are in the
    # Chassis.
    # The root entry for default/hdd(incase of hdd/ssd pools will be
    # modified to use hdd-chassis-<x>
    # instead of using the host directly.
    # The leaf in the rule will be set to use chassis instead of host.
    # Original crush map will be as below for a no hdd/ssd pool.
    # host cmbu-ceph-1 {
    # ...
    # }
    # host cmbu-ceph-2 {
    # ...
    # }
    # host cmbu-ceph-3 {
    # ...
    # }
    # host cmbu-ceph-4 {
    # ...
    # }
    # root default {
    # ...
    # item cmbu-ceph-1 weight 1.090
    # item cmbu-ceph-2 weight 1.090
    # item cmbu-ceph-3 weight 1.090
    # item cmbu-ceph-4 weight 1.090
    # }
    # rule replicated_ruleset {
    # ...
    # step chooseleaf firstn 0 type host
    # step emit
    # }
    # Consider each chassis has 2 nodes. Host1 and Host2 are in same chassis.
    # Host3 and Host4 are in a different chassis. Replica should not happen
    # between Host1 and Host2, similarly it should not happen between Host3
    # and Host4.
    # So the above crushmap will be modified to the following
    # host cmbu-ceph-1 {
    # ...
    # }
    # host cmbu-ceph-2 {
    # ...
    # }
    # host cmbu-ceph-3 {
    # ...
    # }
    # host cmbu-ceph-4 {
    # ...
    # }
    # chassis hdd-chassis-0 {
    # ...
    # item cmbu-ceph-1 weight 1.090
    # item cmbu-ceph-2 weight 1.090
    # }
    # chassis hdd-chassis-1 {
    # ...
    # item cmbu-ceph-3 weight 1.090
    # item cmbu-ceph-4 weight 1.090
    # }
    # root default {
    # ...
    # item hdd-chassis-0 weight 2.180
    # item hdd-chassis-1 weight 2.180
    # }
    # rule replicated_ruleset {
    # ...
    # step chooseleaf firstn 0 type chassis
    # step emit
    # }
    #
    # The above change will ensure that the chassis is the leaf node, which
    # means that the replica created for an object in cmbu-ceph-1 will not
    # be created under cmb-ceph-2 as they belong to the same chassis. Instead
    # it will be put under a node in hdd-chassis-1
    # This code is Idempotent.
    def do_chassis_config(self, input_crush, hosts, chassis_config):
        global crush_id
        global chassis_hdd_ruleset
        global chassis_ssd_ruleset

        if self.is_chassis_disabled(chassis_config) == True:
            return input_crush

        if input_crush == 'none':
            # Get the decoded crush map in txt format
            self.exec_local('sudo ceph osd getcrushmap -o %s' %(CS_CRUSH_MAP))
            self.exec_local('sudo crushtool -d %s -o %s'
                                    %(CS_CRUSH_MAP, CS_CRUSH_MAP_TXT))
        else:
            crush_present = self.exec_local('ls %s | wc -l' %(input_crush))
            if crush_present == '0':
                print 'Crush map not present. Aborting'
                sys.exit(-1)
            self.exec_local('sudo crushtool -d %s -o %s' %(input_crush, CS_CRUSH_MAP_TXT))

        crush_txt_present = self.exec_local('ls %s | wc -l' %(CS_CRUSH_MAP_TXT))
        if crush_txt_present == '0':
            print 'Crush map not present. Aborting'
            sys.exit(-1)

        # If multipool is enabled, we cannot configure chassis
        multipool_enabled = self.exec_local('sudo cat %s | grep hdd-P|wc -l'
                                %(CS_CRUSH_MAP_TXT))
        if multipool_enabled != '0':
            print 'Cannot have both multipool and Chassis config'
            return input_crush

        multipool_enabled = self.exec_local('sudo cat %s | grep ssd-P|wc -l'
                                %(CS_CRUSH_MAP_TXT))
        if multipool_enabled != '0':
            print 'Cannot have both multipool and Chassis config'
            return input_crush

        # Populate the chassis list with chassis id, indexed by hostname.
        host_chassis_info = {}
        chassis_list = {}
        chassis_count = 0
        for hostname in hosts:
            # The chassis_config is the list of host:chassis.
            # for eg: --storage-chassis-config host1:0 host2:0 host3:1 host4:1
            # The loop goes over the entries and finds unique chassis id and
            # creates the list 'chassis_list' indexed with an incrementing
            # starting from 0.
            for chassis in chassis_config:
                chassissplit = chassis.split(':')
                if chassissplit[0] == hostname:
                    host_chassis_info[hostname] = chassissplit[1]
                    #print 'Chassis - %s %s' %(hostname, chassissplit[1])
                    if chassis_count == 0:
                        chassis_list['%d' %(chassis_count)] = chassissplit[1]
                        chassis_count = chassis_count + 1
                    else:
                        tmp_chassis_count = 0
                        while tmp_chassis_count < chassis_count:
                            if chassis_list['%d' %(tmp_chassis_count)] == \
                                            chassissplit[1]:
                                break
                            tmp_chassis_count = tmp_chassis_count + 1
                        if tmp_chassis_count >= chassis_count:
                            chassis_list['%d' %(chassis_count)] = \
                                                            chassissplit[1]
                            chassis_count = chassis_count + 1

        # Find if we have HDD/SSD pools configured.
        # If SSD pool is enabled, then it means that we have two pools
        # otherwise there is only one pool, which is the 'default' pool.
        ssd_pool_enabled = self.exec_local('sudo cat %s | grep "root ssd"|wc -l'
                                %(CS_CRUSH_MAP_TXT))

        root_entries = []
        pool_enabled = 0
        if ssd_pool_enabled != '0':
            pool_enabled = 1
            root_entries.append('hdd')
            root_entries.append('ssd')
        else:
            root_entries.append('default')

        # The "root default", "root hdd" and the "root ssd" are the original root
        # entries that has to be preserved, so that the hdd/ssd pool code or
        # Ceph's osd add code will use them. Also the chassis code will look at
        # the values in these entries and use them for the chassis
        # configuration.

        # Find Root default entry start and end.
        # This will be maintained across modifications
        def_line_str = self.exec_local('cat  %s|grep -n ^root | grep -w default | tail -n 1'
                                %(CS_CRUSH_MAP_TXT))
        def_line_start = int(def_line_str.split(':')[0])
        def_line_end = def_line_start
        while True:
            item_line = self.exec_local('cat %s | tail -n +%d | head -n 1'
                                %(CS_CRUSH_MAP_TXT, def_line_end))
            if item_line.find('}') != -1:
                break
            def_line_end += 1

        # Find the "root hdd" entry start and end.
        # This will be maintained across modifications
        rhdd_line_str = self.exec_local('cat  %s|grep -n ^root | grep -w hdd | tail -n 1'
                                %(CS_CRUSH_MAP_TXT))
        rhdd_line_start = 0
        rhdd_line_end = 0
        if rhdd_line_str != '':
            rhdd_line_start = int(rhdd_line_str.split(':')[0])
            rhdd_line_end = rhdd_line_start
            while True:
                item_line = self.exec_local('cat %s | tail -n +%d | head -n 1'
                                    %(CS_CRUSH_MAP_TXT, rhdd_line_end))
                if item_line.find('}') != -1:
                    break
                rhdd_line_end += 1

        # Find the "root ssd" entry start and end.
        # This will be maintained across modifications
        rssd_line_str = self.exec_local('cat  %s|grep -n ^root | grep -w ssd | tail -n 1'
                                %(CS_CRUSH_MAP_TXT))
        rssd_line_start = 0
        rssd_line_end = 0
        if rssd_line_str != '':
            rssd_line_start = int(rssd_line_str.split(':')[0])
            rssd_line_end = rssd_line_start
            while True:
                item_line = self.exec_local('cat %s | tail -n +%d | head -n 1'
                                    %(CS_CRUSH_MAP_TXT, rssd_line_end))
                if item_line.find('}') != -1:
                    break
                rssd_line_end += 1

        # Find if there are any host configurations after the "root default"
        # These are the hosts for the hdd/ssd pool and has to be maintained
        # across modifications
        # The following code greps for the 'host' entry after the "root default"
        # entry and finds the line number which is storaged in host_line_start.
        # It then finds the last 'host' entry and find the end of the entry by
        # searching for the '}'. These host entries if present, should have been
        # added as part of the HDD/SSD pool. These entries have to be preserved
        # without any modifications. By finding the start and end, the whole
        # section will be added to the modified crush map file.
        host_line_str = self.exec_local('cat  %s | tail -n +%d | grep -n ^host |head -n 1'
                                %(CS_CRUSH_MAP_TXT, def_line_start))
        host_line_start = 0
        host_line_end = 0
        if host_line_str != '':
            host_line_start = def_line_start + \
                                int(host_line_str.split(':')[0]) - 1
            host_line_end_str = self.exec_local('cat  %s | tail -n +%d | grep -n ^host | \
                                tail -n 1'
                                %(CS_CRUSH_MAP_TXT, def_line_start))
            host_line_end =  def_line_start + \
                                int(host_line_end_str.split(':')[0])
            while True:
                item_line = self.exec_local('cat %s | tail -n +%d | head -n 1'
                                    %(CS_CRUSH_MAP_TXT, host_line_end))
                if item_line.find('}') != -1:
                    break
                host_line_end += 1

        # Check if there is already a chassis configuration
        # If present ignore as we'll create again.
        skip_line_str = self.exec_local('cat  %s|grep -n ^chassis |head -n 1'
                                %(CS_CRUSH_MAP_TXT))
        if skip_line_str != '':
            skip_line_num = int(skip_line_str.split(':')[0])
            if skip_line_num > def_line_start:
                skip_line_num = def_line_start
        else:
            skip_line_num = def_line_start

        # Start populating the modified Crush map
        # First populate from beginning till the "root default"
        self.exec_local('cat %s | head -n %d > %s' %(CS_CRUSH_MAP_TXT,
                        (skip_line_num -1), CS_CRUSH_MAP_MOD_TXT))
        # Populate "root default"
        self.exec_local('cat %s | tail -n +%d | head -n %d >> %s' %(CS_CRUSH_MAP_TXT,
                        def_line_start, (def_line_end - def_line_start + 1),
                        CS_CRUSH_MAP_MOD_TXT))
        # Populate host entries for hdd/ssd
        if host_line_start != 0:
            self.exec_local('cat %s | tail -n +%d | head -n %d >> %s' %(CS_CRUSH_MAP_TXT,
                        host_line_start, (host_line_end - host_line_start + 1),
                        CS_CRUSH_MAP_MOD_TXT))
        # Populate "root hdd"
        if rhdd_line_start != 0:
            if rhdd_line_start > host_line_end:
                self.exec_local('cat %s | tail -n +%d | head -n %d >> %s'
                            %(CS_CRUSH_MAP_TXT, rhdd_line_start,
                            (rhdd_line_end - rhdd_line_start + 1),
                            CS_CRUSH_MAP_MOD_TXT))
        # Populate "root ssd"
        if rssd_line_start != 0:
            if rssd_line_start > host_line_end:
                self.exec_local('cat %s | tail -n +%d | head -n %d >> %s'
                            %(CS_CRUSH_MAP_TXT, rssd_line_start,
                            (rssd_line_end - rssd_line_start + 1),
                            CS_CRUSH_MAP_MOD_TXT))

        # Create new root entries for the chassis.
        # use prefix of 'c' for the chassis entries
        # The 'default' will be added as 'cdefault'
        # The 'hdd' will be added as 'chdd'
        # The 'ssd' will be added as 'cssd'
        for entries in root_entries:
            tmp_chassis_count = 0

            self.exec_local('echo "root c%s {" > %s' %(entries,
                            CS_CRUSH_MAP_MOD_TMP_TXT))
            self.exec_local('echo "   id -%d      #do not change unnecessarily" \
                            >> %s' %(crush_id, CS_CRUSH_MAP_MOD_TMP_TXT))
            crush_id += 1
            self.exec_local('echo "   alg straw" >> %s' %(CS_CRUSH_MAP_MOD_TMP_TXT))
            self.exec_local('echo "   hash 0 #rjenkins1" >> %s'
                            %(CS_CRUSH_MAP_MOD_TMP_TXT))
            while tmp_chassis_count < chassis_count:
                total_weight = float('0')
                self.exec_local('echo "chassis chassis-%s-%s {" >> %s' %(entries,
                            tmp_chassis_count, CS_CRUSH_MAP_MOD_TXT))
                self.exec_local('echo "   id -%d      #do not change unnecessarily" \
                            >> %s' %(crush_id, CS_CRUSH_MAP_MOD_TXT))
                crush_id += 1
                self.exec_local('echo "   alg straw" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
                entry_str = self.exec_local('cat  %s|grep -n ^root |grep -w %s |tail -n 1'
                                %(CS_CRUSH_MAP_TXT, entries))
                entry_line_num = int(entry_str.split(':')[0])
                while True:
                    item_line = self.exec_local('cat %s | tail -n +%d | head -n 1'
                                    %(CS_CRUSH_MAP_TXT, entry_line_num))
                    if item_line.find('}') != -1:
                        break
                    if item_line.find('item') != -1:
                        unmod_line = item_line
                        item_line.lstrip()
                        tmp_host_name = item_line.split(' ')[1]
                        tmp_host_name = tmp_host_name.replace('-hdd', '')
                        tmp_host_name = tmp_host_name.replace('-ssd', '')
                        #print tmp_host_name
                        #if tmp_host_name.find('-hdd') != -1 || \
                        #        tmp_host_name.find('-ssd') != -1:
                        if host_chassis_info[tmp_host_name] == \
                                    chassis_list['%d' %(tmp_chassis_count)]:
                            self.exec_local('echo "   %s" >> %s' %(unmod_line,
                                    CS_CRUSH_MAP_MOD_TXT))
                            total_weight += float(item_line.split(' ')[3])
                    entry_line_num += 1
                self.exec_local('echo "}" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
                self.exec_local('echo "   item chassis-%s-%s weight %0.3f" >> %s'
                                %(entries, tmp_chassis_count, total_weight,
                                    CS_CRUSH_MAP_MOD_TMP_TXT))
                tmp_chassis_count += 1
            self.exec_local('echo "}" >> %s' %(CS_CRUSH_MAP_MOD_TMP_TXT))
            self.exec_local('cat %s >> %s' %(CS_CRUSH_MAP_MOD_TMP_TXT,
                                    CS_CRUSH_MAP_MOD_TXT))

        # Now that we have added all the root entries, add the rules
        ruleset = 0
        # Add the default rule
        self.exec_local('echo "rule replicated_ruleset {" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   ruleset %d" >> %s' %(ruleset, CS_CRUSH_MAP_MOD_TXT))
        ruleset += 1
        self.exec_local('echo "   type replicated" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   min_size 1" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   max_size 10" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
        if pool_enabled == 0:
            self.exec_local('echo "   step take cdefault" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step chooseleaf firstn 0 type chassis" >> %s'
                                %(CS_CRUSH_MAP_MOD_TXT))
        else:
            self.exec_local('echo "   step take default" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step chooseleaf firstn 0 type host" >> %s'
                                %(CS_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   step emit" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "}" >> %s' %(CS_CRUSH_MAP_MOD_TXT))

        if pool_enabled == 1:
            # Add the hdd rule
            self.exec_local('echo "rule hdd {" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   ruleset %d" >> %s' %(ruleset, CS_CRUSH_MAP_MOD_TXT))
            chassis_hdd_ruleset = ruleset
            ruleset += 1
            self.exec_local('echo "   type replicated" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   min_size 1" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   max_size 10" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step take chdd" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step chooseleaf firstn 0 type chassis" >> %s'
                                %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step emit" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "}" >> %s' %(CS_CRUSH_MAP_MOD_TXT))

            # Add the ssd rule
            self.exec_local('echo "rule ssd {" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   ruleset %d" >> %s' %(ruleset, CS_CRUSH_MAP_MOD_TXT))
            chassis_ssd_ruleset = ruleset
            ruleset += 1
            self.exec_local('echo "   type replicated" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   min_size 1" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   max_size 10" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step take cssd" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step chooseleaf firstn 0 type chassis" >> %s'
                                %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "   step emit" >> %s' %(CS_CRUSH_MAP_MOD_TXT))
            self.exec_local('echo "}" >> %s' %(CS_CRUSH_MAP_MOD_TXT))

        # Load the new crush map
        self.exec_local('sudo crushtool -c %s -o %s' %(CS_CRUSH_MAP_MOD_TXT,
                                CS_CRUSH_MAP_MOD))
        return CS_CRUSH_MAP_MOD

    #end do_chassis_config()

    # Create HDD/SSD Pool
    # For HDD/SSD pool, the crush map has to be changed to accomodate the
    # rules for the HDD/SSD pools. For this, new ssd, hdd specific hosts
    # have to be added to the map. The ssd, hdd specific maps will then
    # be linked to the root entry for SSD/HDD pool and finally which is linked
    # to a rule entry. The rules will then be applied to the respective pools
    # created using the mkpool command.
    # For populating the map with the host/tier specific entries, A dictionary
    # of host/tier specific entries will be created. This will include the
    # Total tier specific count, tier specific count for a particular host
    # and entries for the tier for a particular host.
    # The host_<tier>_dict will have the tier specific entries and the count
    # for a particular host.
    # The HDD/SSD host/rules additions performed in a loop of pool count.
    # The Pool count is derived from the number of unique pool configured in
    # testbed.py.
    # The pool option is given as part of the disk configuration in the form
    # of '/dev/sdb:/dev/sdc:Pool_1' or '/dev/sdb:Pool_1', based on whether
    # journal is present or not.
    # The following operation is performed.
    # - Get initalized crushmap.
    # - Populate the host HDD/SSD pool entries
    # - Populate the pool specific rules.
    # - Return the modified crush map for further processing
    # host cmbu-ceph-2 {
    # ...
    # item osd.4 weight 0.360
    # }
    # host cmbu-ceph-1 {
    # ...
    # item osd.5 weight 0.180
    # }
    # host cmbu-ceph-4 {
    # ...
    # item osd.6 weight 0.360
    # }
    # host cmbu-ceph-3 {
    # ...
    # item osd.7 weight 0.360
    # }
    # root default {
    # ...
    # item cmbu-ceph-1 weight 1.270
    # item cmbu-ceph-2 weight 1.450
    # item cmbu-ceph-4 weight 1.450
    # item cmbu-ceph-3 weight 1.450
    # }
    # In addition to the above, the following will be added with the
    # hdd/ssd pool based configuration
    # host cmbu-ceph-1-hdd {
    # ...
    # item osd.1 weight 1.090
    # }
    #
    # host cmbu-ceph-2-hdd {
    # ...
    # item osd.0 weight 1.090
    # }
    #
    # host cmbu-ceph-3-hdd {
    # ...
    # item osd.3 weight 1.090
    # }
    #
    # host cmbu-ceph-4-hdd {
    # ...
    # item osd.2 weight 1.090
    # }
    #
    # host cmbu-ceph-1-ssd {
    # ...
    # item osd.5 weight 0.180
    # }
    #
    # host cmbu-ceph-2-ssd {
    # ...
    # item osd.4 weight 0.360
    # }
    #
    # host cmbu-ceph-3-ssd {
    # ...
    # item osd.7 weight 0.360
    # }
    #
    # host cmbu-ceph-4-ssd {
    # ...
    # item osd.6 weight 0.360
    # }
    # root hdd {
    # ...
    # item cmbu-ceph-1-hdd weight 1.090
    # item cmbu-ceph-2-hdd weight 1.090
    # item cmbu-ceph-3-hdd weight 1.090
    # item cmbu-ceph-4-hdd weight 1.090
    # }
    #
    # root ssd {
    # ...
    # item cmbu-ceph-1-ssd weight 0.180
    # item cmbu-ceph-2-ssd weight 0.360
    # item cmbu-ceph-3-ssd weight 0.360
    # item cmbu-ceph-4-ssd weight 0.360
    # }
    #
    # rule replicated_ruleset {
    # ...
    # }
    # rule hdd {
    # ...
    # }
    #
    # rule ssd {
    # ...
    # }
    # Note: This function will not apply the crush map.
    def do_pool_config(self, input_crush, storage_hostnames,
                        storage_disk_config,
                        storage_ssd_disk_config,
                        osd_map_config):
        global host_hdd_dict
        global host_ssd_dict
        global hdd_pool_count
        global ssd_pool_count
        global crush_id

        # If multipool/SSD pool is not enabled, return
        if self.is_multi_pool_disabled(storage_disk_config,
                                        storage_ssd_disk_config) and \
                self.is_ssd_pool_disabled(storage_ssd_disk_config):
            return input_crush

        # Initialize the HDD/SSD pool dictionary.
        # This is used acrros functions to finally
        # set the rules, pg/pgp count, replica size etc.
        pool_count = 0
        while True:
            host_hdd_dict[('totalcount', '%s' %(pool_count))] = 0
            host_ssd_dict[('totalcount', '%s' %(pool_count))] = 0
            host_hdd_dict[('ruleid', '%s' %(pool_count))] = 0
            host_ssd_dict[('ruleid', '%s' %(pool_count))] = 0
            host_hdd_dict[('hostcount', '%s' %(pool_count))] = 0
            host_ssd_dict[('hostcount', '%s' %(pool_count))] = 0
            host_hdd_dict[('poolname', '%s' %(pool_count))] = ''
            host_ssd_dict[('poolname', '%s' %(pool_count))] = ''
            host_hdd_dict[('osdweight', '%s' %(pool_count))] = float('0')
            host_ssd_dict[('osdweight', '%s' %(pool_count))] = float('0')
            if pool_count > MAX_POOL_COUNT:
                break
            pool_count = pool_count + 1

        # Build the host/tier specific dictionary
        for hostname in storage_hostnames:
            host_hdd_dict[hostname, 'count'] = 0
            host_ssd_dict[hostname, 'count'] = 0
            pool_count = 0
            while True:
                host_hdd_dict[('hostcountadded', '%s' %(pool_count))] = 0
                host_ssd_dict[('hostcountadded', '%s' %(pool_count))] = 0
                if pool_count > MAX_POOL_COUNT:
                    break
                pool_count = pool_count + 1
            # Go over all the disk entries
            # Find the unique pool names (for multi pool)
            # Find host count for each pool
            # Populate the corresponding dictionary
            for disks in storage_disk_config:
                diskcount = disks.count(':')
                disksplit = disks.split(':')
                pool_index = 0
                # If there are 3 variables in the disk specification, check
                # if the 3rd entry is a pool name. Always starts with 'P'
                # if there are only 2 variable in the disk specification,
                # check if the check the 2nd entry is the journal disk
                # or the Pool name
                if disksplit[0] == hostname:
                    if (diskcount == 3 and
                        disksplit[3][0] == 'P') or \
                        (diskcount == 2 and
                            disksplit[2][0] == 'P'):
                        if diskcount == 3:
                            pool_name = disksplit[3]
                        if diskcount == 2:
                            pool_name = disksplit[2]
                        # Check if the pool name is already in the dictionary
                        # Otherwise, add it to the dictionary
                        # The host_hdd_dict['poolname', index] will have the
                        # actual poolnames.
                        if hdd_pool_count != 0:
                            while True:
                                if pool_name == host_hdd_dict[('poolname', '%s'
                                                                %(pool_index))]:
                                    break
                                pool_index = pool_index + 1
                                if pool_index == hdd_pool_count:
                                    hdd_pool_count = hdd_pool_count + 1
                                    break
                        else:
                            pool_index = hdd_pool_count
                            hdd_pool_count = hdd_pool_count + 1
                        host_hdd_dict[('poolname', '%s' %(pool_index))] = \
                                                                    pool_name
                    # Populate the Host count for each pool in dictionary.
                    # The hostcountadded dictioary ensures that the host count
                    # is not incremented multiple times for the same host.
                    # The variable is initialized in the top of the loop
                    if host_hdd_dict[('hostcountadded', '%s' %(pool_index))] == 0:
                        host_hdd_dict[('hostcount', '%s' %(pool_index))] += 1
                        host_hdd_dict[('hostcountadded', '%s' %(pool_index))] = 1

            for disks in storage_ssd_disk_config:
                diskcount = disks.count(':')
                disksplit = disks.split(':')
                pool_index = 0
                # If there are 3 variables in the disk specification, check
                # if the 3rd entry is a pool name. Always starts with 'P'
                # if there are only 2 variable in the disk specification,
                # check if the check the 2nd entry is the journal disk
                # or the Pool name
                if disksplit[0] == hostname:
                    if (diskcount == 3 and
                        disksplit[3][0] == 'P') or \
                        (diskcount == 2 and
                            disksplit[2][0] == 'P'):
                        if diskcount == 3:
                            pool_name = disksplit[3]
                        if diskcount == 2:
                            pool_name = disksplit[2]
                        # Check if the pool name is already in the dictionary
                        # Otherwise, add it to the dictionary
                        # The host_hdd_dict['poolname', index] will have the
                        # actual poolnames.
                        if ssd_pool_count != 0:
                            while True:
                                if pool_name == host_ssd_dict[('poolname', '%s'
                                                                %(pool_index))]:
                                    break
                                pool_index = pool_index + 1
                                if pool_index == ssd_pool_count:
                                    ssd_pool_count = ssd_pool_count + 1
                                    break
                        else:
                            pool_index = ssd_pool_count
                            ssd_pool_count = ssd_pool_count + 1
                        host_ssd_dict[('poolname', '%s' %(pool_index))] = \
                                                                    pool_name
                    # Populate the Host count for each pool in dictionary.
                    # The hostcountadded dictioary ensures that the host count
                    # is not incremented multiple times for the same host.
                    # The variable is initialized in the top of the loop
                    if host_ssd_dict[('hostcountadded', '%s' %(pool_index))] == 0:
                        host_ssd_dict[('hostcount', '%s' %(pool_index))] += 1
                        host_ssd_dict[('hostcountadded', '%s' %(pool_index))] = 1

        # Initalize the disk count for each host/pool combination for both HDD
        # and SSD.
        # The dictionary is indexed by the string 'host-pool' and
        # the string 'count'
        for hostname in storage_hostnames:
            pool_index = 0
            while True:
                host_hdd_dict['%s-%s' %(hostname, pool_index), 'count'] = 0
                pool_index = pool_index + 1
                if pool_index >= hdd_pool_count:
                    break
            pool_index = 0
            while True:
                host_ssd_dict['%s-%s' %(hostname, pool_index), 'count'] = 0
                pool_index = pool_index + 1
                if pool_index >= ssd_pool_count:
                    break

        # Find the OSD number corresponding to each HDD/SSD disk and populate
        # the dictionary
        for hostname in storage_hostnames:
            for disks in storage_disk_config:
                disksplit = disks.split(':')
                diskcount = disks.count(':')
                pool_index = 0
                # Get the osd number from the osd_map_config
                # The osd map config will be in the format hostname:/dev/sdb:1
                if disksplit[0] == hostname:
                    for osd_entry in osd_map_config:
                        osdsplit = osd_entry.split(':')
                        if hostname == osdsplit[0] and \
                            disksplit[1] == osdsplit[1]:
                            osdnum = osdsplit[2]
                            break
                    # If there are 3 variables in the disk specification,
                    # check if the 3rd entry is a pool name. Always starts
                    # with 'P'if there are only 2 variable in the disk
                    # specification, check if the check the 2nd entry is the
                    # journal disk or the Pool name
                    if (diskcount == 3 and
                        disksplit[3][0] == 'P') or \
                        (diskcount == 2 and
                            disksplit[2][0] == 'P'):
                        if diskcount == 3:
                            pool_name = disksplit[3]
                        if diskcount == 2:
                            pool_name = disksplit[2]
                        while True:
                            if pool_name == host_hdd_dict[('poolname', '%s'
                                                            %(pool_index))]:
                                break
                            pool_index = pool_index + 1
                            if pool_index >= hdd_pool_count:
                                print 'Cannot find the pool \
                                        name for disk %s' %(disksplit[1])
                                sys.exit(-1)

                    # Populate the OSD number in dictionary referenced by
                    # 'hostname-pool' string and the integer counter.
                    # Then increment the counter which is referenced by
                    # 'hostname-pool' string and the 'count' string.
                    # Also find the total count of OSDs for each pool.
                    host_hdd_dict['%s-%s' %(hostname, pool_index),
                                    host_hdd_dict['%s-%s'
                                        %(hostname, pool_index),'count']] =\
                                                            osdnum
                    host_hdd_dict['%s-%s' %(hostname, pool_index), 'count'] += 1
                    host_hdd_dict[('totalcount', '%s' %(pool_index))] += 1

            for disks in storage_ssd_disk_config:
                disksplit = disks.split(':')
                diskcount = disks.count(':')
                pool_index = 0
                # Get the osd number from the osd_map_config
                # The osd map config will be in the format hostname:/dev/sdb:1
                if disksplit[0] == hostname:
                    for osd_entry in osd_map_config:
                        osdsplit = osd_entry.split(':')
                        if hostname == osdsplit[0] and \
                            disksplit[1] == osdsplit[1]:
                            osdnum = osdsplit[2]
                            break
                    # If there are 3 variables in the disk specification,
                    # check if the 3rd entry is a pool name. Always starts
                    # with 'P' if there are only 2 variable in the disk
                    # specification, check if the check the 2nd entry is the
                    # journal disk or the Pool name
                    if (diskcount == 3 and
                        disksplit[3][0] == 'P') or \
                        (diskcount == 2 and
                            disksplit[2][0] == 'P'):
                        if diskcount == 3:
                            pool_name = disksplit[3]
                        if diskcount == 2:
                            pool_name = disksplit[2]
                        while True:
                            if pool_name == host_ssd_dict[('poolname', '%s'
                                                            %(pool_index))]:
                                break
                            pool_index = pool_index + 1
                            if pool_index >= ssd_pool_count:
                                print 'Cannot find the pool \
                                        name for disk %s' %(disksplit[1])
                                sys.exit(-1)

                    # Populate the OSD number in dictionary referenced by
                    # 'hostname-pool' string and the integer counter.
                    # Then increment the counter which is referenced by
                    # 'hostname-pool' string and the 'count' string.
                    # Also find the total count of OSDs for each pool.
                    host_ssd_dict['%s-%s' %(hostname, pool_index),
                                    host_ssd_dict['%s-%s'
                                        %(hostname, pool_index),'count']] =\
                                                            osdnum
                    host_ssd_dict['%s-%s' %(hostname, pool_index), 'count'] += 1
                    host_ssd_dict[('totalcount', '%s' %(pool_index))] += 1

        #print host_hdd_dict
        #print host_ssd_dict

        # Decompile the Crushmap that we got from the reinit function
        self.exec_local('sudo crushtool -d %s -o %s'
                                    %(input_crush, POOL_CRUSH_MAP_MOD_TXT))

        # Start to populate the -hdd-pool entries for each host/pool.
        for hostname in storage_hostnames:
            pool_index = 0
            while True:
                if host_hdd_dict['%s-%s' %(hostname, pool_index), 'count'] != 0:
                    # This is for single/multi pool HDD
                    # The host entry will be like hostname-hdd or
                    # hostname-hdd-pool name based on whether its a single pool
                    # or multi pool.
                    if hdd_pool_count == 0:
                        self.exec_local('sudo echo "host %s-hdd {" >> %s' %(hostname,
                                                        POOL_CRUSH_MAP_MOD_TXT))
                    else:
                        self.exec_local('sudo echo "host %s-hdd-%s {" >> %s' %(hostname,
                                            host_hdd_dict[('poolname','%s' \
                                                        %(pool_index))],
                                                        POOL_CRUSH_MAP_MOD_TXT))
                    self.exec_local('sudo echo "   id -%d" >> %s'
                                            %(crush_id, POOL_CRUSH_MAP_MOD_TXT))
                    crush_id += 1
                    self.exec_local('sudo echo "   alg straw" >> %s'
                                                    %(POOL_CRUSH_MAP_MOD_TXT))
                    self.exec_local('sudo echo "   hash 0 #rjenkins1" >> %s'
                                                    %(POOL_CRUSH_MAP_MOD_TXT))
                    # We have the Dictionary of OSDs for each host/pool.
                    # We also have the number of OSDs for each host/pool.
                    # Get the count, loop over and poplate the "item osd" for
                    # each OSD.
                    # Get the OSD weight from the existing crush map, This will
                    # be present in the non-hdd/non-ssd host configuration of
                    # the reinitialized crushmap.
                    # During populating, add up all the weights of the OSD and
                    # storage it in a dictionary referenced by string 'osdweight'
                    # and string 'hostname-poolname'.
                    # The total weight will be used when poplating the
                    # "root hdd" or the "root ssd" entry.
                    hsthddcnt = host_hdd_dict['%s-%s' %(hostname, pool_index),
                                                                        'count']
                    total_weight = float('0')
                    while hsthddcnt != 0:
                        hsthddcnt -= 1
                        osd_id = host_hdd_dict['%s-%s' %(hostname, pool_index),
                                                                    hsthddcnt]
                        osd_weight_str = self.exec_local('cat %s | \
                                                    grep -w "item osd.%s" | \
                                                    head -n 1 | \
                                                    awk \'{print $4}\''
                                                    %(POOL_CRUSH_MAP_MOD_TXT,
                                                        osd_id))
                        osd_weight = float('%s' %(osd_weight_str))
                        self.exec_local('sudo echo "   item osd.%s weight %0.3f" >> %s'
                                                        %(osd_id, osd_weight,
                                                        POOL_CRUSH_MAP_MOD_TXT))
                        total_weight += osd_weight
                    self.exec_local('sudo echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                    self.exec_local('sudo echo "" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                    host_hdd_dict[('osdweight', '%s-%s'
                                        %(hostname, pool_index))] = total_weight
                pool_index = pool_index + 1
                if pool_index >= hdd_pool_count:
                    break

        # Start to populate the -ssd-pool entries for each host/pool.
        for hostname in storage_hostnames:
            pool_index = 0
            while True:
                if host_ssd_dict['%s-%s' %(hostname, pool_index), 'count'] != 0:
                    # This is for single/multi pool HDD
                    # The host entry will be like hostname-ssd or
                    # hostname-ssd-pool name based on whether its a single pool
                    # or multi pool.
                    if ssd_pool_count == 0:
                        self.exec_local('sudo echo "host %s-ssd {" >> %s' %(hostname,
                                                        POOL_CRUSH_MAP_MOD_TXT))
                    else:
                        self.exec_local('sudo echo "host %s-ssd-%s {" >> %s' %(hostname,
                                            host_ssd_dict[('poolname','%s' \
                                                        %(pool_index))],
                                                        POOL_CRUSH_MAP_MOD_TXT))
                    self.exec_local('sudo echo "   id -%d" >> %s' %(crush_id,
                                                        POOL_CRUSH_MAP_MOD_TXT))
                    crush_id += 1
                    self.exec_local('sudo echo "   alg straw" >> %s'
                                                    %(POOL_CRUSH_MAP_MOD_TXT))
                    self.exec_local('sudo echo "   hash 0 #rjenkins1" >> %s'
                                                    %(POOL_CRUSH_MAP_MOD_TXT))
                    # We have the Dictionary of OSDs for each host/pool.
                    # We also have the number of OSDs for each host/pool.
                    # Get the count, loop over and poplate the "item osd" for
                    # each OSD.
                    # Get the OSD weight from the existing crush map, This will
                    # be present in the non-hdd/non-ssd host configuration of
                    # the reinitialized crushmap.
                    # During populating, add up all the weights of the OSD and
                    # storage it in a dictionary referenced by string 'osdweight'
                    # and string 'hostname-poolname'.
                    # The total weight will be used when poplating the
                    # "root hdd" or the "root ssd" entry.
                    hstssdcnt = host_ssd_dict['%s-%s' %(hostname, pool_index),
                                                                        'count']
                    total_weight = float('0')
                    while hstssdcnt != 0:
                        hstssdcnt -= 1
                        osd_id = host_ssd_dict['%s-%s' %(hostname, pool_index),
                                                                    hstssdcnt]
                        osd_weight_str = self.exec_local('cat %s | \
                                                    grep -w "item osd.%s" | \
                                                    head -n 1 | \
                                                    awk \'{print $4}\''
                                                    %(POOL_CRUSH_MAP_MOD_TXT,
                                                        osd_id))
                        osd_weight = float('%s' %(osd_weight_str))
                        self.exec_local('sudo echo "   item osd.%s weight %0.3f" >> %s'
                                                        %(osd_id, osd_weight,
                                                        POOL_CRUSH_MAP_MOD_TXT))
                        total_weight += osd_weight
                    self.exec_local('sudo echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                    self.exec_local('sudo echo "" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                    host_ssd_dict[('osdweight', '%s-%s'
                                    %(hostname, pool_index))] = total_weight
                pool_index = pool_index + 1
                if pool_index >= ssd_pool_count:
                    break

        # Add root entries for hdd/ssd
        if storage_disk_config[0] != 'none':
            pool_index = 0
            while True:
                # Populate the "root hdd" for single pool
                # or "root hdd-poolname" for multi pool.
                if hdd_pool_count == 0:
                    self.exec_local('sudo echo "root hdd {" >> %s'
                                    %(POOL_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('sudo echo "root hdd-%s {" >> %s'
                            %(host_hdd_dict[('poolname','%s' %(pool_index))],
                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   id -%d" >> %s'
                                    %(crush_id, POOL_CRUSH_MAP_MOD_TXT))
                crush_id += 1
                self.exec_local('sudo echo "   alg straw" >> %s'
                                    %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   hash 0 #rjenkins1" >> %s'
                                    %(POOL_CRUSH_MAP_MOD_TXT))
                # We have the list of hosts/pool dictionary as well as the
                # total osd weight for each host/pool.
                # Populate the "item hostname-hdd" for single pool or
                # the "item hostname-hdd-poolname" for multi pool, based on
                # the osd count referenced by the string 'hostname-poolname' and
                # 'count'
                for hostname in storage_hostnames:
                    if host_hdd_dict['%s-%s' %(hostname, pool_index),'count'] != 0:
                        if hdd_pool_count == 0:
                            self.exec_local('sudo echo "   item %s-hdd weight %0.3f" >> %s'
                                            %(hostname,
                                            host_hdd_dict[('osdweight',
                                                '%s-%s' %(hostname, pool_index))],
                                            POOL_CRUSH_MAP_MOD_TXT))
                        else:
                            self.exec_local('sudo echo "   item %s-hdd-%s weight %0.3f" >> %s'
                                            %(hostname,
                                            host_hdd_dict[('poolname',
                                                '%s' %(pool_index))],
                                            host_hdd_dict[('osdweight',
                                                '%s-%s' %(hostname, pool_index))],
                                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                pool_index = pool_index + 1
                if pool_index >= hdd_pool_count:
                    break

        if storage_ssd_disk_config[0] != 'none':
            pool_index = 0
            while True:
                # Populate the "root ssd" for single pool
                # or "root ssd-poolname" for multi pool.
                if ssd_pool_count == 0:
                    self.exec_local('sudo echo "root ssd {" >> %s'
                                    %(POOL_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('sudo echo "root ssd-%s {" >> %s'
                            %(host_ssd_dict[('poolname','%s' %(pool_index))],
                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   id -%d" >> %s'
                                    %(crush_id, POOL_CRUSH_MAP_MOD_TXT))
                crush_id += 1
                self.exec_local('sudo echo "   alg straw" >> %s'
                                    %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   hash 0 #rjenkins1" >> %s'
                                    %(POOL_CRUSH_MAP_MOD_TXT))
                # We have the list of hosts/pool dictionary as well as the
                # total osd weight for each host/pool.
                # Populate the "item hostname-ssd" for single pool or
                # the "item hostname-ssd-poolname" for multi pool, based on
                # the osd count referenced by the string 'hostname-poolname' and
                # 'count'
                for hostname in storage_hostnames:
                    if host_ssd_dict['%s-%s' %(hostname, pool_index),'count'] != 0:
                        if ssd_pool_count == 0:
                            self.exec_local('sudo echo "   item %s-ssd weight %0.3f" >> %s'
                                            %(hostname,
                                            host_ssd_dict[('osdweight',
                                                '%s-%s' %(hostname, pool_index))],
                                            POOL_CRUSH_MAP_MOD_TXT))
                        else:
                            self.exec_local('sudo echo "   item %s-ssd-%s weight %0.3f" >> %s'
                                            %(hostname,
                                            host_ssd_dict[('poolname',
                                                '%s' %(pool_index))],
                                            host_ssd_dict[('osdweight',
                                                '%s-%s' %(hostname, pool_index))],
                                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                pool_index = pool_index + 1
                if pool_index >= ssd_pool_count:
                    break

        # Add ruleset
        ruleset = 0
        # Add the default rule
        # We populate this as we have removed this during the reinitialize.
        self.exec_local('echo "rule replicated_ruleset {" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   ruleset %d" >> %s' %(ruleset, POOL_CRUSH_MAP_MOD_TXT))
        ruleset += 1
        self.exec_local('echo "   type replicated" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   min_size 1" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   max_size 10" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   step take default" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   step chooseleaf firstn 0 type host" >> %s'
                                                %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "   step emit" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
        self.exec_local('echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))

        # Add rules for HDD/HDD pools
        if storage_disk_config[0] != 'none':
            pool_index = 0
            while True:
                if hdd_pool_count == 0:
                    self.exec_local('sudo echo "rule hdd {" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('sudo echo "rule hdd-%s {" >> %s'
                            %(host_hdd_dict[('poolname','%s' %(pool_index))],
                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   ruleset %d" >> %s'
                                            %(ruleset, POOL_CRUSH_MAP_MOD_TXT))
                host_hdd_dict[('ruleid', '%s' %(pool_index))] = ruleset
                ruleset += 1
                self.exec_local('sudo echo "   type replicated" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   min_size 0" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   max_size 10" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                if hdd_pool_count == 0:
                    self.exec_local('sudo echo "   step take hdd" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('sudo echo "   step take hdd-%s" >> %s'
                            %(host_hdd_dict[('poolname','%s' %(pool_index))],
                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   step chooseleaf firstn 0 type host" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   step emit" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                pool_index = pool_index + 1
                if pool_index >= hdd_pool_count:
                    break

        # Add rules for SSD/SSD pools
        if storage_ssd_disk_config[0] != 'none':
            pool_index = 0
            while True:
                if ssd_pool_count == 0:
                    self.exec_local('sudo echo "rule ssd {" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('sudo echo "rule ssd-%s {" >> %s'
                            %(host_ssd_dict[('poolname','%s' %(pool_index))],
                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   ruleset %d" >> %s'
                                            %(ruleset, POOL_CRUSH_MAP_MOD_TXT))
                host_ssd_dict[('ruleid', '%s' %(pool_index))] = ruleset
                ruleset += 1
                self.exec_local('sudo echo "   type replicated" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   min_size 0" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   max_size 10" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                if ssd_pool_count == 0:
                    self.exec_local('sudo echo "   step take ssd" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                else:
                    self.exec_local('sudo echo "   step take ssd-%s" >> %s'
                            %(host_ssd_dict[('poolname','%s' %(pool_index))],
                            POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   step chooseleaf firstn 0 type host" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "   step emit" >> %s'
                                            %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "}" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                self.exec_local('sudo echo "" >> %s' %(POOL_CRUSH_MAP_MOD_TXT))
                pool_index = pool_index + 1
                if pool_index >= ssd_pool_count:
                    break

        # Compile the crushmap and return for further processing
        self.exec_local('sudo crushtool -c %s -o %s' %(POOL_CRUSH_MAP_MOD_TXT,
                                                        POOL_CRUSH_MAP_MOD))
        return POOL_CRUSH_MAP_MOD

    #end do_pool_config()

    # Function to configure Ceph Object storage
    def do_configure_object_storage_pools(self, object_store_pool):

        parent_pool_list = ['%s' %(object_store_pool),
                            'volumes_%s' %(object_store_pool),
                            'volumes_hdd_%s' %(object_store_pool),
                            'volumes_ssd_%s' %(object_store_pool),
                            'volumes']

        for pool in parent_pool_list:
            pool_available = self.exec_local('rados lspools | grep -w %s$ | wc -l'
                                            %(object_store_pool))
            if pool_available != '0':
                crush_ruleset =  self.exec_local('sudo ceph osd pool get %s \
                                    crush_ruleset | awk \'{print $2}\'' %(pool))
                replica =  self.exec_local('sudo ceph osd pool get %s \
                                    size | awk \'{print $2}\'' %(pool))
                pg_num =  self.exec_local('sudo ceph osd pool get %s \
                                    pg_num | awk \'{print $2}\'' %(pool))
                osd_count = int(pg_num)/30
                break

        for pool in ceph_object_store_pools:
            pool_present = self.exec_local('sudo rados lspools | grep -w "%s$" | \
                                            wc -l' %(pool))
            if pool_present == '0':
                self.exec_local('sudo rados mkpool %s' %(pool))
            self.exec_local('sudo ceph osd pool set %s crush_ruleset %s'
                                    %(pool, crush_ruleset))
            self.exec_local('sudo ceph osd pool set %s size %s'
                                    %(pool, replica))
            if pool != '.rgw':
                osd_ncount = osd_count/2
            else:
                osd_ncount = osd_count
            self.set_pg_pgp_count(osd_ncount, pool, 0)
        return
    #end do_configure_object_storage_pools()

    # Removes unwanted pools
    def do_remove_unwanted_pools(self):
        # Remove unwanted pools
        pool_present = self.exec_local('sudo rados lspools | grep -w data | wc -l')
        if pool_present != '0':
            self.exec_local('sudo rados rmpool data data --yes-i-really-really-mean-it')
        pool_present = self.exec_local('sudo rados lspools | grep -w metadata | wc -l')
        if pool_present != '0':
            self.exec_local('sudo rados rmpool metadata metadata \
                                --yes-i-really-really-mean-it')
        pool_present = self.exec_local('sudo rados lspools | grep -w rbd | wc -l')
        if pool_present != '0':
            self.exec_local('sudo rados rmpool rbd rbd --yes-i-really-really-mean-it')
    #end do_remove_unwanted_pools()

    # Function for pool configuration
    # Removes unwanted pools
    # Create default images/volumes pool
    # Create HDD/SSD pools
    # Sets PG/PGP count.
    # Sets ruleset based on pool/chassis configuration
    def do_configure_pools(self, storage_hostnames, storage_disk_config,
                            storage_ssd_disk_config, chassis_config,
                            replica_size = None, ssd_cache_tier = False,
                            ceph_object_storage = False,
                            object_store_pool = 'volumes'):
        global host_hdd_dict
        global host_ssd_dict
        global hdd_pool_count
        global ssd_pool_count
        global ceph_pool_list
        global ceph_tier_list
        global chassis_hdd_ruleset
        global chassis_ssd_ruleset

        # Remove unwanted pools
        pool_present = self.exec_local('sudo rados lspools | grep -w data | wc -l')
        if pool_present != '0':
            self.exec_local('sudo rados rmpool data data --yes-i-really-really-mean-it')
        pool_present = self.exec_local('sudo rados lspools | grep -w metadata | wc -l')
        if pool_present != '0':
            self.exec_local('sudo rados rmpool metadata metadata \
                                --yes-i-really-really-mean-it')
        pool_present = self.exec_local('sudo rados lspools | grep -w rbd | wc -l')
        if pool_present != '0':
            self.exec_local('sudo rados rmpool rbd rbd --yes-i-really-really-mean-it')

        # Add required pools
        pool_present = self.exec_local('sudo rados lspools | grep -w volumes | wc -l')
        if pool_present == '0':
            self.exec_local('sudo rados mkpool volumes')
        pool_present = self.exec_local('sudo rados lspools | grep -w images | wc -l')
        if pool_present == '0':
            self.exec_local('sudo rados mkpool images')

        # HDD/SSD/Multipool enabled
        if self.is_multi_pool_disabled(storage_disk_config,
                                        storage_ssd_disk_config) == FALSE or \
                        self.is_ssd_pool_disabled(storage_ssd_disk_config) == FALSE:
            # Create HDD pools
            # If multi-pool is present, then create pool in the name of
            # volume_hdd_'poolname' otherwize create volume_hdd pool
            # Set the crush ruleset for each pool
            # Set PG/PGP count based on the dictionary values
            # Set replica based on host count
            # Create ceph_pool_list with the list of new poolnames. This will
            # be used during virsh configuration
            if storage_disk_config[0] != 'none':
                pool_index = 0
                while True:
                    if hdd_pool_count == 0:
                        pool_present = self.exec_local('sudo rados lspools | \
                                                grep -w volumes_hdd | wc -l')
                        if pool_present == '0':
                            self.exec_local('sudo rados mkpool volumes_hdd')
                        self.exec_local('sudo ceph osd pool set \
                                    volumes_hdd crush_ruleset %d'
                                    %(host_hdd_dict[('ruleid', '%s'
                                        %(pool_index))]))
                        if host_hdd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                            self.exec_local('sudo ceph osd pool set volumes_hdd size %s'
                                                            %(REPLICA_ONE))
                        elif replica_size != 'None':
                            self.exec_local('sudo ceph osd pool set volumes_hdd size %s'
                                                            %(replica_size))
                        else:
                            self.exec_local('sudo ceph osd pool set volumes_hdd size %s'
                                                            %(REPLICA_DEFAULT))
                        self.set_pg_pgp_count(host_hdd_dict[('totalcount', '%s'
                                                %(pool_index))], 'volumes_hdd',
                                                host_hdd_dict[('hostcount', '%s'
                                                %(pool_index))])
                        ceph_pool_list.append('volumes_hdd')
                    else:
                        pool_present = self.exec_local('sudo rados lspools | \
                                                grep -w volumes_hdd_%s | wc -l'
                                                %(host_hdd_dict[('poolname','%s'
                                                %(pool_index))]))
                        if pool_present == '0':
                            self.exec_local('sudo rados mkpool volumes_hdd_%s'
                                        %(host_hdd_dict[('poolname','%s'
                                        %(pool_index))]))
                        self.exec_local('sudo ceph osd pool set \
                                        volumes_hdd_%s crush_ruleset %d'
                                        %(host_hdd_dict[('poolname','%s'
                                        %(pool_index))],
                                        host_hdd_dict[('ruleid', '%s'
                                        %(pool_index))]))
                        if host_hdd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                            self.exec_local('sudo ceph osd pool set volumes_hdd_%s size %s'
                                        %(host_hdd_dict[('poolname','%s'
                                        %(pool_index))], REPLICA_ONE))
                        elif replica_size != 'None':
                            self.exec_local('sudo ceph osd pool set volumes_hdd_%s size %s'
                                        %(host_hdd_dict[('poolname','%s'
                                        %(pool_index))], replica_size))
                        else:
                            self.exec_local('sudo ceph osd pool set volumes_hdd_%s size %s'
                                        %(host_hdd_dict[('poolname','%s'
                                        %(pool_index))], REPLICA_DEFAULT))
                        self.set_pg_pgp_count(host_hdd_dict[('totalcount', '%s'
                                                %(pool_index))],'volumes_hdd_%s'
                                                %(host_hdd_dict[('poolname','%s'
                                                %(pool_index))]),
                                                host_hdd_dict[('hostcount', '%s'
                                                %(pool_index))])
                        ceph_pool_list.append('volumes_hdd_%s'
                                                %(host_hdd_dict[('poolname',
                                                '%s' %(pool_index))]))
                    pool_index = pool_index + 1
                    if pool_index >= hdd_pool_count:
                        break

                # Set ruleset for the default volumes/images pool
                pool_index = 0
                self.exec_local('sudo ceph osd pool set images crush_ruleset %d'
                                %(host_hdd_dict[('ruleid','%s' %(pool_index))]))
                self.exec_local('sudo ceph osd pool set volumes crush_ruleset %d'
                                %(host_hdd_dict[('ruleid','%s' %(pool_index))]))

                # Set the replica for the default volumes/images pool
                if host_hdd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                    self.exec_local('sudo ceph osd pool set volumes size %s'
                                                    %(REPLICA_ONE))
                    self.exec_local('sudo ceph osd pool set images size %s'
                                                    %(REPLICA_ONE))
                elif replica_size != 'None':
                    self.exec_local('sudo ceph osd pool set volumes size %s'
                                                    %(replica_size))
                    self.exec_local('sudo ceph osd pool set images size %s'
                                                    %(replica_size))
                else:
                    self.exec_local('sudo ceph osd pool set volumes size %s'
                                                    %(REPLICA_DEFAULT))
                    self.exec_local('sudo ceph osd pool set images size %s'
                                                    %(REPLICA_DEFAULT))

                # Set the pg/pgp count for the default volumes/images pool
                self.set_pg_pgp_count(
                            host_hdd_dict[('totalcount', '%s' %(pool_index))],
                            'volumes',
                            host_hdd_dict[('hostcount', '%s' %(pool_index))])
                self.set_pg_pgp_count(
                            host_hdd_dict[('totalcount', '%s' %(pool_index))],
                            'images',
                            host_hdd_dict[('hostcount', '%s' %(pool_index))])

            # Create SSD pools
            # If multi-pool is present, then create pool in the name of
            # volume_ssd_'poolname' otherwize create volume_ssd pool
            # Set the crush ruleset for each pool
            # Set PG/PGP count based on the dictionary values
            # Set replica based on host count
            # Create ceph_pool_list with the list of new poolnames. This will
            # be used during virsh configuration
            if storage_ssd_disk_config[0] != 'none':
                pool_index = 0
                while True:
                    if ssd_pool_count == 0:
                        pool_present = self.exec_local('sudo rados lspools | \
                                                grep -w volumes_ssd | wc -l')
                        if pool_present == '0':
                            self.exec_local('sudo rados mkpool volumes_ssd')
                        self.exec_local('sudo ceph osd pool set \
                                    volumes_ssd crush_ruleset %d'
                                    %(host_ssd_dict[('ruleid', '%s'
                                        %(pool_index))]))
                        if host_ssd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                            self.exec_local('sudo ceph osd pool set volumes_ssd size %s'
                                                %(REPLICA_ONE))
                        elif replica_size != 'None':
                            self.exec_local('sudo ceph osd pool set volumes_ssd size %s'
                                                %(replica_size))
                        else:
                            self.exec_local('sudo ceph osd pool set volumes_ssd size %s'
                                                %(REPLICA_DEFAULT))
                        self.set_pg_pgp_count(host_ssd_dict[('totalcount', '%s'
                                                %(pool_index))], 'volumes_ssd',
                                                host_ssd_dict[('hostcount', '%s'
                                                %(pool_index))])
                        ceph_pool_list.append('volumes_ssd')
                    else:
                        pool_present = self.exec_local('sudo rados lspools | \
                                                grep -w volumes_ssd_%s | wc -l'
                                                %(host_ssd_dict[('poolname','%s'
                                                %(pool_index))]))
                        if pool_present == '0':
                            self.exec_local('sudo rados mkpool volumes_ssd_%s'
                                        %(host_ssd_dict[('poolname','%s'
                                        %(pool_index))]))
                        self.exec_local('sudo ceph osd pool set \
                                        volumes_ssd_%s crush_ruleset %d'
                                        %(host_ssd_dict[('poolname','%s'
                                        %(pool_index))],
                                        host_ssd_dict[('ruleid', '%s'
                                        %(pool_index))]))
                        if host_ssd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                            self.exec_local('sudo ceph osd pool set volumes_ssd_%s size %s'
                                        %(host_ssd_dict[('poolname','%s'
                                        %(pool_index))], REPLICA_DEFAULT))
                        elif replica_size != 'None':
                            self.exec_local('sudo ceph osd pool set volumes_ssd_%s size %s'
                                        %(host_ssd_dict[('poolname','%s'
                                        %(pool_index))], replica_size))
                        else:
                            self.exec_local('sudo ceph osd pool set volumes_ssd_%s size %s'
                                        %(host_ssd_dict[('poolname','%s'
                                        %(pool_index))], REPLICA_DEFAULT))
                        self.set_pg_pgp_count(host_ssd_dict[('totalcount', '%s'
                                                %(pool_index))],'volumes_ssd_%s'
                                                %(host_ssd_dict[('poolname','%s'
                                                %(pool_index))]),
                                                host_ssd_dict[('hostcount', '%s'
                                                %(pool_index))])
                        ceph_pool_list.append('volumes_ssd_%s'
                                                %(host_ssd_dict[('poolname',
                                                '%s' %(pool_index))]))
                    pool_index = pool_index + 1
                    if pool_index >= ssd_pool_count:
                        break

            if ssd_cache_tier == 'True' and storage_ssd_disk_config[0] != 'none':
                pool_index = 0
                while True:
                    if hdd_pool_count == 0:
                        pool_present = self.exec_local('sudo rados lspools | \
                                                grep -w ssd_tier | wc -l')
                        if pool_present == '0':
                            self.exec_local('sudo rados mkpool ssd_tier')
                        self.exec_local('sudo ceph osd pool set \
                                    ssd_tier crush_ruleset %d'
                                    %(host_ssd_dict[('ruleid', '%s'
                                        %(pool_index))]))
                        if host_ssd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                            self.exec_local('sudo ceph osd pool set ssd_tier size %s'
                                                %(REPLICA_ONE))
                        elif replica_size != 'None':
                            self.exec_local('sudo ceph osd pool set ssd_tier size %s'
                                                %(replica_size))
                        else:
                            self.exec_local('sudo ceph osd pool set ssd_tier size %s'
                                                %(REPLICA_DEFAULT))
                        self.set_pg_pgp_count(host_ssd_dict[('totalcount', '%s'
                                                %(pool_index))], 'ssd_tier',
                                                host_ssd_dict[('hostcount', '%s'
                                                %(pool_index))])
                        ceph_tier_list.append('ssd_tier')
                    else:
                        if hdd_pool_count == ssd_pool_count:
                            pool_name = host_hdd_dict[('poolname',
                                                        '%s' %(pool_index))]
                            rule_id = host_ssd_dict[('ruleid',
                                                        '%s'%(pool_index))]
                            host_count = host_ssd_dict[('hostcount',
                                                        '%s' %(pool_index))]
                            total_count = host_ssd_dict[('totalcount',
                                                        '%s' %(pool_index))]
                        else:
                            pool_name = host_hdd_dict[('poolname',
                                                        '%s' %(pool_index))]
                            rule_id = host_ssd_dict[('ruleid','0')]
                            host_count = host_ssd_dict[('hostcount', '0')]
                            total_count = host_ssd_dict[('totalcount', '0')]
                        pool_present = self.exec_local('sudo rados lspools | \
                                                grep -w ssd_tier_%s | wc -l'
                                                %(pool_name))
                        if pool_present == '0':
                            self.exec_local('sudo rados mkpool ssd_tier_%s'
                                        %(pool_name))
                        self.exec_local('sudo ceph osd pool set \
                                        ssd_tier_%s crush_ruleset %d'
                                        %(pool_name, rule_id))
                        if host_hdd_dict[('hostcount', '%s' %(pool_index))] <= 1:
                            self.exec_local('sudo ceph osd pool set ssd_tier_%s size %s'
                                        %(pool_name, REPLICA_ONE))
                        elif replica_size != 'None':
                            self.exec_local('sudo ceph osd pool set ssd_tier_%s size %s'
                                        %(pool_name, replica_size))
                        else:
                            self.exec_local('sudo ceph osd pool set ssd_tier_%s size %s'
                                        %(pool_name, REPLICA_DEFAULT))
                        self.set_pg_pgp_count(total_count,
                                        'ssd_tier_%s' %(pool_name), host_count)
                        ceph_tier_list.append('ssd_tier_%s' %(pool_name))
                    pool_index = pool_index + 1
                    if pool_index >= hdd_pool_count:
                        break
        # Without HDD/SSD pool
        else:
            # Find the host count
            host_count = 0
            for hostname in storage_hostnames:
                for disks in storage_disk_config:
                    disksplit = disks.split(':')
                    if hostname == disksplit[0]:
                        host_count += 1
                        break

            # Set replica size based on host count
            if host_count <= 1:
                self.exec_local('sudo ceph osd pool set volumes size %s'
                                    %(REPLICA_ONE))
                self.exec_local('sudo ceph osd pool set images size %s'
                                    %(REPLICA_ONE))
            elif host_count == 2:
                self.exec_local('sudo ceph osd pool set volumes size %s'
                                    %(REPLICA_TWO))
                self.exec_local('sudo ceph osd pool set images size %s'
                                    %(REPLICA_TWO))
            elif replica_size != 'None':
                self.exec_local('sudo ceph osd pool set volumes size %s'
                                    %(replica_size))
                self.exec_local('sudo ceph osd pool set images size %s'
                                    %(replica_size))
            else:
                rep_size = self.exec_local('sudo ceph osd pool get volumes size | \
                                    awk \'{print $2}\'')
                if rep_size != REPLICA_DEFAULT:
                    self.exec_local('sudo ceph osd pool set volumes size %s'
                                    %(REPLICA_DEFAULT))
                rep_size = self.exec_local('sudo ceph osd pool get images size | \
                                    awk \'{print $2}\'')
                if rep_size != REPLICA_DEFAULT:
                    self.exec_local('sudo ceph osd pool set images size %s'
                                    %(REPLICA_DEFAULT))

            # Set PG/PGP count based on osd new count
            osd_count = int(self.exec_local('sudo ceph osd ls |wc -l'))
            self.set_pg_pgp_count(osd_count, 'images', host_count)
            self.set_pg_pgp_count(osd_count, 'volumes', host_count)

        if self.is_chassis_disabled(chassis_config) == FALSE:
            if self.is_ssd_pool_disabled(storage_ssd_disk_config) == FALSE:
                self.exec_local('sudo ceph osd pool set volumes_hdd crush_ruleset %d'
                                                %(chassis_hdd_ruleset))
                self.exec_local('sudo ceph osd pool set volumes_ssd crush_ruleset %d'
                                                %(chassis_ssd_ruleset))
                self.exec_local('sudo ceph osd pool set images crush_ruleset %d'
                                                %(chassis_hdd_ruleset))
                self.exec_local('sudo ceph osd pool set volumes crush_ruleset %d'
                                                %(chassis_hdd_ruleset))
            else:
                self.exec_local('sudo ceph osd pool set images crush_ruleset 0')
                self.exec_local('sudo ceph osd pool set volumes crush_ruleset 0')
        if ceph_object_storage == 'True':
            self.do_configure_object_storage_pools(object_store_pool)
        return {'ceph_pool_list': ceph_pool_list, 'ceph_tier_list': ceph_tier_list}
    #end do_configure_pools()

    def create_and_apply_cinder_patch(self):
        self.exec_locals('echo \"--- a/manager.py   2015-06-24 00:08:23.871395783 -0700\" \
                > %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"+++ b/manager.py   2015-06-24 00:11:46.856401389 -0700\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"@@ -636,7 +636,8 @@\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"             volume = self.db.volume_get(context, volume_id)\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"             volume_metadata = self.db.volume_admin_metadata_get(\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"                 context.elevated(), volume_id)\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"-            if volume[\'status\'] == \'attaching\':\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"+            if (volume[\'status\'] == \'attaching\' or\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"+                volume[\'status\'] == \'in-use\'):\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"                 if (volume[\'instance_uuid\'] and volume[\'instance_uuid\'] !=\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"                         instance_uuid):" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_locals('echo \"                     msg = _(\\"being attached by another instance\\")\" \
                >> %s' %(CINDER_PATCH_FILE))
        self.exec_local('patch -N %s %s'
                %(CINDER_VOLUME_MGR_PY, CINDER_PATCH_FILE))
        return
    #end create_and_apply_cinder_patch

    def create_and_apply_ceph_deploy_patch(self):
        ceph_dep_version = self.exec_locals('dpkg-query -W -f=\'${Version}\' ceph-deploy')
        if LooseVersion(ceph_dep_version) >= LooseVersion('1.5.0'):
            ceph_new_version = True
        else:
            ceph_new_version = False
        self.exec_locals('echo \"diff -Naur ceph_deploy/hosts/debian/mon/create.py ceph_deploy.new/hosts/debian/mon/create.py" \
                > %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"--- ceph_deploy/hosts/debian/mon/create.py      2013-10-07 11:50:13.000000000 -0700" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"+++ ceph_deploy.new/hosts/debian/mon/create.py  2015-11-10 17:17:02.784241000 -0800" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        if ceph_new_version == True:
            self.exec_locals('echo \"@@ -3,7 +3,7 @@" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
            self.exec_locals('echo \" from ceph_deploy.lib import remoto" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        else:
            self.exec_locals('echo \"@@ -2,9 +2,9 @@" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
            self.exec_locals('echo \" from ceph_deploy.lib.remoto import process" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"-def create(distro, args, monitor_keyring):" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"+def create(distro, args, monitor_keyring, hostname):" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        if ceph_new_version != True:
            self.exec_locals('echo \"     logger = distro.conn.logger" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"-    hostname = distro.conn.remote_module.shortname()" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"+    #hostname = distro.conn.remote_module.shortname()" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"     common.mon_create(distro, args, monitor_keyring, hostname)" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        if ceph_new_version != True:
            self.exec_locals('echo \"     service = distro.conn.remote_module.which_service()" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"diff -Naur ceph_deploy/mon.py ceph_deploy.new/mon.py" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"--- ceph_deploy/mon.py  2013-10-07 11:50:13.000000000 -0700" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"+++ ceph_deploy.new/mon.py      2015-11-10 17:16:22.524241000 -0800" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"@@ -201,7 +201,7 @@" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"             # ensure remote hostname is good to go" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"             hostname_is_compatible(distro.sudo_conn, rlogger, name)" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"             rlogger.debug(\'deploying mon to %%s\', name)" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"-            distro.mon.create(distro, args, monitor_keyring)" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"+            distro.mon.create(distro, args, monitor_keyring, name)" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"             # tell me the status of the deployed mon" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_locals('echo \"             time.sleep(2)  # give some room to start" \
                >> %s' %(CEPH_DEPLOY_PATCH_FILE))
        self.exec_local('cd /usr/lib/python2.7/dist-packages/ && patch -N -p0 <%s'
                %(CEPH_DEPLOY_PATCH_FILE))
    #end create_and_apply_ceph_deploy_patch

    # Function to configure Ceph cache tier
    def do_configure_ceph_cache_tier(self, ceph_pool_list, ceph_tier_list,
                                     storage_replica_size):
        num_hdd_pool = len(ceph_tier_list)
        if num_hdd_pool == 0:
            return
        index = 0
        for entry in ceph_pool_list:
            if index >= num_hdd_pool:
                return
            total_ssd_size_st = self.exec_local('sudo ceph df | grep -w %s | \
                                awk \'{print $5}\''
                                %(ceph_tier_list[index]))
            size_mult_st = total_ssd_size_st[len(total_ssd_size_st) - 1]
            if size_mult_st == 'T':
                size_mult = 1024 * 1024 * 1024 * 1024
            elif size_mult_st == 'G':
                size_mult = 1024 * 1024 * 1024
            elif size_mult_st == 'M':
                size_mult = 1024 * 1024
            elif size_mult_st == 'K':
                size_mult = 1024
            total_ssd_size = int(total_ssd_size_st[:-1])
            total_ssd_size = total_ssd_size * size_mult
            if storage_replica_size != 'None':
                replica_size = int(storage_replica_size)
            else:
                replica_size = 2
            cache_size = total_ssd_size / replica_size
            self.exec_locals('sudo ceph osd tier add %s %s'
                    %(ceph_pool_list[index], ceph_tier_list[index]))
            self.exec_locals('sudo ceph osd tier cache-mode %s writeback'
                    %(ceph_tier_list[index]))
            self.exec_locals('sudo ceph osd tier set-overlay %s %s'
                    %(ceph_pool_list[index], ceph_tier_list[index]))
            self.exec_locals('sudo ceph osd pool set %s hit_set_type bloom'
                    %(ceph_tier_list[index]))
            self.exec_locals('sudo ceph osd pool set %s hit_set_count 1'
                    %(ceph_tier_list[index]))
            self.exec_locals('sudo ceph osd pool set %s hit_set_period 3600'
                    %(ceph_tier_list[index]))
            self.exec_locals('sudo ceph osd pool set %s target_max_bytes %s'
                    %(ceph_tier_list[index], cache_size))
            self.exec_locals('ceph osd pool set %s min_read_recency_for_promote 1'
                    %(ceph_tier_list[index]))
            index += 1
        return
    #end do_configure_ceph_cache_tier

# Function to configure Object storage
# Specifically defined outside of class so that it can be called
# from fab and SM.
def configure_object_storage(is_master, is_os_host, new_apache,
                               storage_os_hosts, storage_master,
                               curr_hostname):
    storage_os_hosts = storage_os_hosts.split()
    ceph_utils = SetupCephUtils()
    if storage_os_hosts[0] == 'none':
        ceph_utils.exec_local('sudo ceph auth get-or-create \
                        client.radosgw.gateway osd \
                        \'allow rwx\' mon \'allow rwx\' -o %s'
                        %(RADOS_KEYRING))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway host %s'
                %(ETC_CEPH_CONF, storage_master))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway keyring %s'
                %(ETC_CEPH_CONF, RADOS_KEYRING))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway \'log file\' %s'
                %(ETC_CEPH_CONF, RADOS_GW_LOG_FILE))
        if new_apache == 0:
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway \'rgw socket path\' \"\"'
                    %(ETC_CEPH_CONF))
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway \'rgw frontends\' \'%s\''
                    %(ETC_CEPH_CONF, RADOS_GW_FRONT_END))
        else:
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway \'rgw socket path\' %s'
                    %(ETC_CEPH_CONF, RADOS_GW_SOCKET_PATH))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway \'rgw print continue\' false'
                %(ETC_CEPH_CONF))
        if is_master == 1 or is_os_host == 1:
            ceph_utils.exec_local('sudo mkdir -p %s' %(LIB_RADOS_GW))
            ceph_utils.exec_local('sudo touch %s/done' %(LIB_RADOS_GW))
    else:

        ceph_utils.exec_local('sudo ceph auth get-or-create \
                        client.radosgw.gateway_%s osd \
                        \'allow rwx\' mon \'allow rwx\' -o %s_%s'
                        %(storage_master, RADOS_KEYRING, storage_master))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway_%s host %s'
                %(ETC_CEPH_CONF, storage_master, storage_master))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway_%s keyring %s_%s'
                %(ETC_CEPH_CONF, storage_master, RADOS_KEYRING, storage_master))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway_%s \'log file\' %s'
                %(ETC_CEPH_CONF, storage_master, RADOS_GW_LOG_FILE))
        if new_apache == 0:
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s \'rgw socket path\' \"\"'
                    %(ETC_CEPH_CONF, storage_master))
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s \'rgw frontends\' \'%s\''
                    %(ETC_CEPH_CONF, storage_master, RADOS_GW_FRONT_END))
        else:
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s \'rgw socket path\' %s'
                    %(ETC_CEPH_CONF, storage_master, RADOS_GW_SOCKET_PATH))
        ceph_utils.exec_local('sudo openstack-config --set \
                %s client.radosgw.gateway_%s \'rgw print continue\' false'
                %(ETC_CEPH_CONF, storage_master))

        for entry in storage_os_hosts:
            ceph_utils.exec_local('sudo ceph auth get-or-create \
                        client.radosgw.gateway_%s osd \
                        \'allow rwx\' mon \'allow rwx\' -o %s_%s'
                        %(entry, RADOS_KEYRING, entry))
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s host %s'
                    %(ETC_CEPH_CONF, entry, entry))
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s keyring %s_%s'
                    %(ETC_CEPH_CONF, entry, RADOS_KEYRING, entry))
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s \'log file\' %s'
                    %(ETC_CEPH_CONF, entry, RADOS_GW_LOG_FILE))
            if new_apache == 0:
                ceph_utils.exec_local('sudo openstack-config --set \
                        %s client.radosgw.gateway_%s \'rgw socket path\' \"\"'
                        %(ETC_CEPH_CONF, entry))
                ceph_utils.exec_local('sudo openstack-config --set \
                        %s client.radosgw.gateway_%s \'rgw frontends\' \'%s\''
                        %(ETC_CEPH_CONF, entry, RADOS_GW_FRONT_END))
            else:
                ceph_utils.exec_local('sudo openstack-config --set \
                        %s client.radosgw.gateway_%s \'rgw socket path\' %s'
                        %(ETC_CEPH_CONF, entry, RADOS_GW_SOCKET_PATH))
            ceph_utils.exec_local('sudo openstack-config --set \
                    %s client.radosgw.gateway_%s \'rgw print continue\' false'
                    %(ETC_CEPH_CONF, entry))
        if is_master == 1 or is_os_host == 1:
            ceph_utils.exec_local('sudo mkdir -p %s_%s'
                                    %(LIB_RADOS_GW, curr_hostname))
            ceph_utils.exec_local('sudo touch %s_%s/done'
                                    %(LIB_RADOS_GW, curr_hostname))

    if is_master == 1 or is_os_host == 1:
        ceph_utils.exec_local('sudo service radosgw-all restart')
        # Apache configurations
        ceph_utils.exec_local('sudo a2enmod rewrite')
        ceph_utils.exec_local('sudo a2enmod proxy_http')
        ceph_utils.exec_local('sudo a2enmod proxy_fcgi')
        ceph_utils.exec_locals('sudo echo \"Listen 9001\" > %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"<VirtualHost *:9001>\" >> %s'
                                %(APACHE_RGW_CONF))
        #ceph_utils.exec_local('sudo echo "ServerName localhost" >> %s'
        #                        %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"DocumentRoot /var/www/html\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"ErrorLog /var/log/apache2/rgw_error.log\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"CustomLog /var/log/apache2/rgw_access.log combined\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"# LogLevel debug\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"RewriteEngine On\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"RewriteRule .* - [E=HTTP_AUTHORIZATION:%%{HTTP:Authorization},L]\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"SetEnv proxy-nokeepalive 1\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        if new_apache == 0:
            ceph_utils.exec_locals('sudo echo \"ProxyPass / fcgi://localhost:9000/\" >> %s'
                                    %(APACHE_RGW_CONF))
        else:
            ceph_utils.exec_locals('sudo echo \"ProxyPass / unix:///var/run/ceph/ceph.radosgw.gateway.fastcgi.sock|fcgi://localhost:9000/\" >> %s'
                                    %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_locals('sudo echo \"</VirtualHost>\" >> %s'
                                %(APACHE_RGW_CONF))
        ceph_utils.exec_local('sudo a2enconf rgw')
        ceph_utils.exec_local('service apache2 restart')
    if is_master == 1:
        contrail_user = ceph_utils.exec_local('radosgw-admin --uid contrail user \
                                              info 2>/dev/null | grep contrail | \
                                              wc -l')
        print contrail_user
        if contrail_user == '0':
            ceph_utils.exec_local('sudo radosgw-admin user create --uid="contrail" \
                                   --display-name="Demo User"')
            ceph_utils.exec_local('sudo radosgw-admin subuser create --uid=contrail \
                                   --subuser=contrail:swift --access=full')
            ceph_utils.exec_local('sudo radosgw-admin key create \
                                   --subuser=contrail:swift --key-type=swift \
                                   --gen-secret')
        access_key = ceph_utils.exec_locals('sudo radosgw-admin --uid contrail \
                                   user info | \
                                   grep -A 2 \"\\\"user\\\": \\\"contrail\\\"\" | \
                                   grep access_key | awk \'{print $2}\' | \
                                   cut -d \'\"\' -f 2')
        secret_key = ceph_utils.exec_locals('sudo radosgw-admin --uid contrail \
                                   user info | \
                                   grep -A 2 \"\\\"user\\\": \\\"contrail\\\"\" | \
                                   grep secret_key | awk \'{print $2}\' | \
                                   cut -d \'\"\' -f 2')
        swift_key = ceph_utils.exec_locals('sudo radosgw-admin --uid contrail \
                                   user info | \
                                   grep -A 3 swift_keys | \
                                   grep secret_key | awk \'{print $2}\' | \
                                   cut -d \'\"\' -f 2')
        ceph_utils.exec_locals('sudo echo S3 Authentication > %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo ----------------- >> %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo username: contrail >> %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo S3 access_key: %s >> %s'
                               %(access_key, OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo S3 secret_key: %s >> %s'
                               %(secret_key, OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo "" >> %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo Swift Authentication >> %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo -------------------- >> %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo username: contrail:swift >> %s'
                               %(OBJECT_STORAGE_USER_FILE))
        ceph_utils.exec_locals('sudo echo Swift secret_key = %s >> %s'
                               %(swift_key, OBJECT_STORAGE_USER_FILE))

#end configure_object_storage
