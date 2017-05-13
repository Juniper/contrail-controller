#!/usr/bin/python

import argparse
import ConfigParser

import platform
import sys
import os
from ceph_utils import SetupCephUtils

class SetupCeph(object):

    # Global variables used across functions
    # The following are read/writable globals
    # Ceph storage disk list, populated during journal initialization
    global storage_disk_list
    storage_disk_list = []
    global TRUE
    TRUE = 1
    global FALSE
    FALSE = 0


    # Function to get form the storage disk list based on the journal
    # configurations.
    def get_storage_disk_list(self):
        # Setup Journal disks
        # Convert configuration from --storage-journal-config to ":" format
        # for example --storage-disk-config ceph1:/dev/sdb ceph1:/dev/sdc
        # --storage-journal-config ceph1:/dev/sdd will be stored in
        # storage_disk_list as ceph1:/dev/sdb:/dev/sdd, ceph1:/dev/sdc:/dev/sdd

        global storage_disk_list
        new_storage_disk_list = []

        # If there is no 'journal' configuration, may be its inline in disk
        # Just use the storage_disk_config/storage_ssd_disk_config
        for hostname in self._args.storage_hostnames:
            for disks in self._args.storage_disk_config:
                disksplit = disks.split(':')
                if disksplit[0] == hostname:
                    storage_disk_list.append(disks)
        if self._args.storage_ssd_disk_config[0] != 'none':
            for hostname in self._args.storage_hostnames:
                for ssd_disks in self._args.storage_ssd_disk_config:
                    disksplit = ssd_disks.split(':')
                    if disksplit[0] == hostname:
                        storage_disk_list.append(ssd_disks)

        # Remove the Pool numbers from the disk list. The pool name should
        # always start with 'P'
        for disks in storage_disk_list:
            journal_available = disks.count(':')
            disksplit = disks.split(':')
            diskcount = disks.count(':')
            if diskcount == 3:
                if disksplit[3][0] == 'P':
                    new_storage_disk_list.append('%s:%s:%s' %(disksplit[0],
                                                    disksplit[1], disksplit[2]))
            elif diskcount == 2:
                if disksplit[2][0] == 'P':
                    new_storage_disk_list.append('%s:%s' %(disksplit[0],
                                                    disksplit[1]))
                else:
                    new_storage_disk_list.append('%s:%s:%s' %(disksplit[0],
                                                    disksplit[1], disksplit[2]))
            else:
                new_storage_disk_list.append(disks)
        return new_storage_disk_list
    #end get_storage_disk_list()

    # Function to check if multipool is disabled or not
    # Returns False if enabled
    # Returns True if disabled
    # Checks for 'P' (for Pool) entry in the disk list in
    # the 2nd or 3rd field.
    def is_multi_pool_disabled(self):
        global storage_disk_list

        for disks in storage_disk_list:
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
    def is_ssd_pool_disabled(self):
        if self._args.storage_ssd_disk_config[0] == 'none':
            return TRUE
        else:
            return FALSE
    #end is_ssd_pool_disabled()

    # Function to check if Chassis configuration is disabled or not
    # Returns False if enabled
    # Returns True if disabled
    def is_chassis_disabled(self):
        if self._args.storage_chassis_config[0] == 'none':
            return TRUE
        else:
            return FALSE
    #end is_chassis_disabled()

    # Top level function for crush map changes
    def do_crush_map_pool_config(self):
        global ceph_pool_list
        global ceph_tier_list

        crush_setup_utils = SetupCephUtils()

        # If there is no mutlipool/ssd pool/chassis configuration, return
        if self.is_multi_pool_disabled() != TRUE or \
                self.is_ssd_pool_disabled() != TRUE or \
                self.is_chassis_disabled() != TRUE:

            # Initialize crush map
            crush_map = crush_setup_utils.initialize_crush()
            # Do pool configuration
            crush_map = crush_setup_utils.do_pool_config(crush_map,
                                            self._args.storage_hostnames,
                                            self._args.storage_disk_config,
                                            self._args.storage_ssd_disk_config,
                                            self._args.storage_osd_map)
            # Do chassis configuration
            crush_map = crush_setup_utils.do_chassis_config(crush_map,
                                            self._args.storage_hostnames,
                                            self._args.storage_chassis_config)
            # Apply crushmap
            crush_setup_utils.apply_crush(crush_map)

        # Configure Pools
        result = crush_setup_utils.do_configure_pools(
                                        self._args.storage_hostnames,
                                        self._args.storage_disk_config,
                                        self._args.storage_ssd_disk_config,
                                        self._args.storage_chassis_config,
                                        self._args.replica_size,
                                        self._args.ssd_cache_tier,
                                        self._args.object_storage,
                                        self._args.object_storage_pool)
        # TODO: possibly return this back to the
        # ansible code to perform ssd tier provisioning
        ceph_pool_list = result['ceph_pool_list']
        ceph_tier_list = result['ceph_tier_list']
    #end do_crush_map_pool_config()


    # Main function for storage related configurations
    # Note: All the functions are idempotent. Any additions/modifications
    #       should ensure that the behavior stays the same.
    def __init__(self, args_str = None):
        #print sys.argv[1:]
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])

        # Parse the arguments
        self._parse_args(args_str)

        # prepare storage disk list
        self.get_storage_disk_list()

        # Do crush/pool configuration
        self.do_crush_map_pool_config()


    def _parse_args(self, args_str):

        # Source any specified config/ini file
        # Turn off help, so we show all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")

        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'storage_disk_config': ['none'],
            'storage_ssd_disk_config': ['none'],
            'storage_chassis_config': ['none'],
            'replica_size': '2',
            'storage_osd_map': ['none'],
            'ssd_cache_tier': False,
            'object_storage': False,
            'object_store_pool': 'volumes'
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

        parser.add_argument("--storage-hostnames", help = "Host names of storage nodes", nargs='+', type=str)
        parser.add_argument("--storage-disk-config", help = "Disk list to be used for distrubuted storage", nargs="+", type=str)
        parser.add_argument("--storage-ssd-disk-config", help = "SSD Disk list to be used for distrubuted storage", nargs="+", type=str)
        parser.add_argument("--storage-chassis-config", help = "Chassis ID for the host to avoid replication between nodes in the same chassis", nargs="+", type=str)
        parser.add_argument("--storage-osd-map", help = "Disk to osd number map", nargs="+", type=str)
        parser.add_argument("--ssd-cache-tier", help = "Enable SSD cache tier")
        parser.add_argument("--object-storage", help = "Enable Ceph object storage")
        parser.add_argument("--object-storage-pool", help = "Ceph object storage pool")
        parser.add_argument("--replica-size", help = "Ceph object storage pool")

        self._args = parser.parse_args(remaining_argv)

    #end _parse_args


def main(args_str = None):
    SetupCeph(args_str)
#end main

if __name__ == "__main__":
    main()

