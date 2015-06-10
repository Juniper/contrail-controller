# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor DB to store VM, SI information
"""
import pycassa
from pycassa.system_manager import *
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from sandesh_common.vns.constants import SVC_MONITOR_KEYSPACE_NAME, \
    CASSANDRA_DEFAULT_GC_GRACE_SECONDS
import inspect
from cfgm_common import jsonutils as json
import time


class ServiceMonitorDB(object):

    _KEYSPACE = SVC_MONITOR_KEYSPACE_NAME

    def __init__(self, args=None):
        self._args = args

        if args.cluster_id:
            self._keyspace = '%s_%s' % (args.cluster_id,
                                        ServiceMonitorDB._KEYSPACE)
        else:
            self._keyspace = ServiceMonitorDB._KEYSPACE


    # update with logger instance
    def add_logger(self, logger):
        self._logger = logger

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

    def _db_remove(self, table, key, columns=None):
        try:
            if columns:
                table.remove(key, columns=columns)
            else:
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
    def _cassandra_init(self, cf_name):
        server_idx = 0
        num_dbnodes = len(self._args.cassandra_server_list)
        connected = False

        # Update connection info
        self._logger.db_conn_status_update(ConnectionStatus.INIT,
            self._args.cassandra_server_list)
        while not connected:
            try:
                cass_server = self._args.cassandra_server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                # Update connection info
                self._logger.db_conn_status_update(ConnectionStatus.DOWN,
                    [cass_server], str(e))
                server_idx = (server_idx + 1) % num_dbnodes
                time.sleep(3)

        # Update connection info
        self._logger.db_conn_status_update(ConnectionStatus.UP,
            self._args.cassandra_server_list)

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
        column_families = [cf_name]
        gc_grace_sec = CASSANDRA_DEFAULT_GC_GRACE_SECONDS
        for cf in column_families:
            try:
                sys_mgr.create_column_family(self._keyspace, cf, gc_grace_seconds=gc_grace_sec)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                print "Warning! " + str(e)
                sys_mgr.alter_column_family(self._keyspace, cf, gc_grace_seconds=gc_grace_sec)

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
        svc_cf = pycassa.ColumnFamily(conn_pool, cf_name,
            read_consistency_level=rd_consistency,
            write_consistency_level=wr_consistency)
        return svc_cf

class ServiceInstanceDB(ServiceMonitorDB):

    _SVC_SI_CF = 'service_instance_table'

    def init_database(self):
        self._svc_si_cf = self._cassandra_init(self._SVC_SI_CF)

    def get_vm_db_prefix(self, inst_count):
        return('vm' + str(inst_count) + '-')

    def remove_vm_info(self, si_fq_str, vm_uuid):
        si_info = self.service_instance_get(si_fq_str)
        if not si_info:
            return

        prefix = None
        for key, item in si_info.items():
            if item == vm_uuid:
                prefix = key.split('-')[0]
                break
        if not prefix:
            return

        vm_column_list = []
        for key in si_info.keys():
            if key.startswith(prefix):
                vm_column_list.append(key)
        self.service_instance_remove(si_fq_str, vm_column_list)

    # service instance CRUD
    def service_instance_get(self, si_fq_str):
        return self._db_get(self._svc_si_cf, si_fq_str)

    def service_instance_insert(self, si_fq_str, entry):
        return self._db_insert(self._svc_si_cf, si_fq_str, entry)

    def service_instance_remove(self, si_fq_str, columns=None):
        return self._db_remove(self._svc_si_cf, si_fq_str, columns)

    def service_instance_list(self):
        return self._db_list(self._svc_si_cf)

class LBDB(ServiceMonitorDB):

    _LB_CF = 'pool_table'

    def init_database(self):
        self._lb_cf = self._cassandra_init(self._LB_CF)

    def pool_config_get(self, pool_id):
        json_str = self._db_get(self._lb_cf, pool_id)
        if json_str and 'config_info' in json_str:
            return json.loads(json_str['config_info'])
        else:
            return None

    def pool_driver_info_get(self, pool_id):
        json_str = self._db_get(self._lb_cf, pool_id)
        if json_str and 'driver_info' in json_str:
            return json.loads(json_str['driver_info'])
        else:
            return None

    def pool_config_insert(self, pool_id, pool_obj):
        entry = json.dumps(pool_obj)
        return self._db_insert(self._lb_cf, pool_id, {'config_info': entry})

    def pool_driver_info_insert(self, pool_id, pool_obj):
        entry = json.dumps(pool_obj)
        return self._db_insert(self._lb_cf, pool_id, {'driver_info': entry})

    def pool_remove(self, pool_id, columns=None):
        return self._db_remove(self._lb_cf, pool_id, columns)

    def pool_list(self):
        ret_list = []
        for each_entry_id, each_entry_data in self._db_list(self._lb_cf) or []:
            config_info_obj_dict = json.loads(each_entry_data['config_info'])
            driver_info_obj_dict = None
            if 'driver_info' in each_entry_data:
                driver_info_obj_dict = json.loads(each_entry_data['driver_info'])
            ret_list.append((each_entry_id, config_info_obj_dict, driver_info_obj_dict))
        return ret_list
