# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
F5 DB to store Pool and associated objects
"""
import pycassa
from pycassa.system_manager import *

import json
import time


class F5LBDB(object):

    _KEYSPACE = 'f5_lb_keyspace'
    _F5_LB_CF = 'pool_table'

    def __init__(self, args):
        self._args = args

        if args.cluster_id:
            self._keyspace = '%s_%s' % (args.cluster_id,
                                        F5LBDB._KEYSPACE)
        else:
            self._keyspace = F5LBDB._KEYSPACE

    def init_database(self):
        self._cassandra_init()

    def pool_get(self, pool_id):
        json_str = self._db_get(self._f5_lb_cf, pool_id)
        if json_str:
            return json.loads(json_str['info'])
        else:
            return None

    def pool_insert(self, pool_id, pool_obj):
        entry = json.dumps(pool_obj)
        return self._db_insert(self._f5_lb_cf, pool_id, {'info': entry})

    def pool_remove(self, pool_id, columns=None):
        return self._db_remove(self._f5_lb_cf, pool_id, columns)

    def pool_list(self):
        ret_list = []
        for each_entry in self._db_list(self._f5_lb_cf) or []:
            obj_dict = json.loads(each_entry['info'])
            ret_list.append(obj_dict)
        return ret_list

    # db CRUD
    def _db_get(self, table, key):
        try:
            entry = table.get(key)
        except Exception as e:
            return None

        return entry

    def _db_insert(self, table, key, entry):
        try:
            table.insert(key, entry)
        except Exception as e:
            return False

        return True

    def _db_remove(self, table, key, columns=None):
        try:
            if columns:
                table.remove(key, columns=columns)
            else:
                table.remove(key)
        except Exception as e:
            return False

        return True

    def _db_list(self, table):
        try:
            entries = list(table.get_range())
        except Exception as e:
            return None

        return entries


    # initialize cassandra
    def _cassandra_init(self):
        server_idx = 0
        num_dbnodes = len(self._args.cassandra_server_list)
        connected = False

        while not connected:
            try:
                cass_server = self._args.cassandra_server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                server_idx = (server_idx + 1) % num_dbnodes
                time.sleep(3)

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
        column_families = [self._F5_LB_CF]
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
        self._f5_lb_cf = pycassa.ColumnFamily(conn_pool, self._F5_LB_CF,
            read_consistency_level=rd_consistency,
            write_consistency_level=wr_consistency)
