#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db.py
# Implementation of database purging
#

import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily
from pycassa.types import *
from pycassa import *
from sandesh.viz.constants import *
from pysandesh.util import UTCTimestampUsec

class AnalyticsDb(object):
    def __init__(self, logger, cassandra_server_list):
        self._logger = logger
        self._cassandra_server_list = cassandra_server_list
        self._sysm = None
        self._pool = None
        self.connect_db()
        self._sysm = self._get_sysm()
        self.number_of_purge_requests = 0
    # end __init__

    def connect_db(self):
        try:
            self._pool = pycassa.ConnectionPool(COLLECTOR_KEYSPACE,
                          server_list=self._cassandra_server_list, timeout=None)
            self._logger.info("Connection to AnalyticsDb is Established!")
        except Exception as e:
            self._logger.error("Exception: Failure in connection to AnalyticsDb %s" % e)
    # end connect_db

    def _get_sysm(self):
        try:
            for server_and_port in self._cassandra_server_list:
                _sysm = pycassa.system_manager.SystemManager(server_and_port)
                if (_sysm is not None):
                    break
        except Exception as e:
            self._logger.error("Exception: SystemManager failed %s" % e)
        else:
            return _sysm
    # end _get_sysm

    def _get_analytics_start_time(self):
        try:
            col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
            row = col_family.get(SYSTEM_OBJECT_ANALYTICS, columns=[SYSTEM_OBJECT_START_TIME])
        except Exception as e:
            self._logger.error("Exception: analytics_start_time Failure %s" % e)
            return None
        else:
            return row[SYSTEM_OBJECT_START_TIME]
    # end _get_analytics_start_time

    def _update_analytics_start_time(self, start_time):
        try:
            col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
            col_family.insert(SYSTEM_OBJECT_ANALYTICS,
                    {SYSTEM_OBJECT_START_TIME: start_time})
        except Exception as e:
            self._logger.error("Exception: update_analytics_start_time Connection Failure %s" % e)
    # end _update_analytics_start_time

    def purge_old_data(self, purge_time, purge_id):
        total_rows_deleted = 0 # total number of rows deleted
        try:
            table_list = self._sysm.get_keyspace_column_families(COLLECTOR_KEYSPACE)
        except Exception as e:
            self._logger.error("Exception: Purge_id %s Failed to get Analytics Column families%s" % (purge_id, e))
            return 0
        excluded_table_list = ['MessageTable', 'FlowRecordTable', 'MessageTableTimestamp', 'SystemObjectTable']
        for table in table_list:
            # purge from index tables
            if (table not in excluded_table_list):
                self._logger.info("purge_id %s deleting old records from table: %s" % (purge_id, str(table)))
                per_table_deleted = 0 # total number of rows deleted from each table
                try:
                    cf = pycassa.ColumnFamily(self._pool, table)
                except Exception as e:
                    self._logger.error("purge_id %s Failure in fetching the columnfamily %s" % e)
                    continue
                b = cf.batch()
                try:
                    for key, cols in cf.get_range():
                        t2 = key[0]
                        # each row will have equivalent of 2^23 = 8388608 usecs
                        row_time = (float(t2)*pow(2, RowTimeInBits))
                        if (row_time < purge_time):
                            per_table_deleted +=1
                            total_rows_deleted +=1
                            b.remove(key)
                    b.send()
                except Exception as e:
                    self._logger.error("Exception: Purge_id %s This table doesnot have row time %s" % (purge_id, e))
                    continue
                self._logger.info("Purge_id %s deleted %d rows from table : %s" % (purge_id, per_table_deleted, table))
        self._logger.info("Purge_id %s total rows deleted: %s" % (purge_id, total_rows_deleted))
        return total_rows_deleted
    # end purge_old_data

    def db_purge(self, purge_input, purge_id):
        total_rows_deleted = 0 # total number of rows deleted
        if (purge_input != None):
            current_time = UTCTimestampUsec()
            analytics_start_time = float(self._get_analytics_start_time())
            purge_time = analytics_start_time + (float((purge_input)*(float(current_time) - analytics_start_time)))/100
            total_rows_deleted = self.purge_old_data(purge_time, purge_id)
            if (total_rows_deleted != 0):
                self._update_analytics_start_time(int(purge_time))
        return total_rows_deleted
    # end db_purge

# end AnalyticsDb
