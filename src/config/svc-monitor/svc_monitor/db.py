# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor DB to store VM, SI information
"""
import pycassa
from pycassa.system_manager import *

import inspect
import time


class ServiceMonitorDB(object):

    _KEYSPACE = 'svc_monitor_keyspace'
    _SVC_VM_CF = 'svc_vm_table'
    _SVC_SI_CF = 'svc_si_table'
    _SVC_CLEANUP_CF = 'svc_cleanup_table'

    def __init__(self, args=None):
        self._args = args

        if args.cluster_id:
            self._keyspace = '%s_%s' % (args.cluster_id,
                                        ServiceMonitorDB._KEYSPACE)
        else:
            self._keyspace = ServiceMonitorDB._KEYSPACE

        self._cassandra_init()

    # update with logger instance
    def add_logger(self, logger):
        self._logger = logger


    # service instance CRUD
    def service_instance_get(self, si_fq_str):
        return self._db_get(self._svc_si_cf, si_fq_str)

    def service_instance_insert(self, si_fq_str, entry):
        return self._db_insert(self._svc_si_cf, si_fq_str, entry)

    def service_instance_remove(self, si_fq_str):
        return self._db_remove(self._svc_si_cf, si_fq_str)

    def service_instance_list(self):
        return self._db_list(self._svc_si_cf)


    # virtual machine CRUD
    def virtual_machine_get(self, vm_uuid):
        return self._db_get(self._svc_vm_cf, vm_uuid)

    def virtual_machine_insert(self, vm_uuid, entry):
        return self._db_insert(self._svc_vm_cf, vm_uuid, entry)

    def virtual_machine_remove(self, vm_uuid):
        return self._db_remove(self._svc_vm_cf, vm_uuid)

    def virtual_machine_list(self):
        return self._db_list(self._svc_vm_cf)


    # cleanup table CRUD
    def cleanup_table_get(self, key):
        return self._db_get(self._svc_cleanup_cf, key)

    def cleanup_table_insert(self, key, entry):
        return self._db_insert(self._svc_cleanup_cf, key, entry)

    def cleanup_table_remove(self, key):
        return self._db_remove(self._svc_cleanup_cf, key)

    def cleanup_table_list(self):
        return self._db_list(self._svc_cleanup_cf)


    # db CRUD
    def _db_get(self, table, key):
        try:
            entry = table.get(key)
        except Exception as e:
            self._logger.log("DB: %s %s get failed" %
                             (inspect.stack()[1][3], key))
            return None

        return entry

    def _db_insert(self, table, key, entry):
        try:
            table.insert(key, entry)
        except Exception as e:
            self._logger.log("DB: %s %s insert failed" %
                             (inspect.stack()[1][3], key))
            return False

        return True

    def _db_remove(self, table, key):
        try:
            table.remove(key)
        except Exception as e:
            self._logger.log("DB: %s %s remove failed" %
                             (inspect.stack()[1][3], key))
            return False

        return True

    def _db_list(self, table):
        try:
            entries = list(table.get_range())
        except Exception as e:
            self._logger.log("DB: %s list failed" %
                             (inspect.stack()[1][3]))
            return None

        return entries


    # initialize cassandra
    def _cassandra_init(self):
       server_idx = 0
       num_dbnodes = len(self._args.cassandra_server_list)
       connected = False

       # Update connection info
       '''TODO
       ConnectionState.update(conn_type = ConnectionType.DATABASE,
           name = 'Database', status = ConnectionStatus.INIT,
           message = '', server_addrs = self._args.cassandra_server_list)
       '''
   
       while not connected:
           try:
               cass_server = self._args.cassandra_server_list[server_idx]
               sys_mgr = SystemManager(cass_server)
               connected = True
           except Exception as e:
               # Update connection info
               '''TODO
               ConnectionState.update(conn_type = ConnectionType.DATABASE,
                   name = 'Database', status = ConnectionStatus.DOWN,
                   message = '', server_addrs = [cass_server])
               '''
               server_idx = (server_idx + 1) % num_dbnodes
               time.sleep(3)
   
   
       # Update connection info
       '''TODO
       ConnectionState.update(conn_type = ConnectionType.DATABASE,
           name = 'Database', status = ConnectionStatus.UP,
           message = '', server_addrs = self._args.cassandra_server_list)
       '''
   
       if self._args.reset_config:
           try:
               sys_mgr.drop_keyspace(self._keyspace)
           except pycassa.cassandra.ttypes.InvalidRequestException as e:
               print "Warning! " + str(e)
   
       try:
           sys_mgr.create_keyspace(self._keyspace, SIMPLE_STRATEGY,
                                   {'replication_factor': str(num_dbnodes)})
       except pycassa.cassandra.ttypes.InvalidRequestException as e:
           print "Warning! " + str(e)
   
       # set up column families
       column_families = [self._SVC_VM_CF,
                          self._SVC_CLEANUP_CF,
                          self._SVC_SI_CF]
       for cf in column_families:
           try:
               sys_mgr.create_column_family(self._keyspace, cf)
           except pycassa.cassandra.ttypes.InvalidRequestException as e:
               print "Warning! " + str(e)
   
       conn_pool = pycassa.ConnectionPool(self._keyspace,
                                          self._args.cassandra_server_list,
                                          max_overflow=10,
                                          use_threadlocal=True,
                                          prefill=True,
                                          pool_size=10,
                                          pool_timeout=30,
                                          max_retries=-1,
                                          timeout=0.5)
   
       rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
       wr_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
       self._svc_vm_cf = pycassa.ColumnFamily(conn_pool, self._SVC_VM_CF,
           read_consistency_level=rd_consistency,
           write_consistency_level=wr_consistency)
       self._svc_si_cf = pycassa.ColumnFamily(conn_pool, self._SVC_SI_CF,
           read_consistency_level=rd_consistency,
           write_consistency_level=wr_consistency)
       self._svc_cleanup_cf = pycassa.ColumnFamily(conn_pool,
           self._SVC_CLEANUP_CF, read_consistency_level=rd_consistency,
           write_consistency_level=wr_consistency)
