#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db.py
# For Database Purge Process
#
import sys
import readline # optional, will allow Up/Down/History in the console
import code
import random
import uuid

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
        try:
            self._pool = pycassa.ConnectionPool('ContrailAnalytics', server_list=self._cassandra_server_list, timeout=None)
            self._logger.info("running\n")
            for server_port in self._cassandra_server_list:
                self._sysm = pycassa.system_manager.SystemManager(server_port)
        except Exception as e:
            self._logger.error("Exception: %s" % e)
    # end __init__
  
    def _analytics_start_time(self):
        col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
        row = col_family.get(SYSTEM_OBJECT_ANALYTICS, columns=[SYSTEM_OBJECT_START_TIME])
        return row[SYSTEM_OBJECT_START_TIME]
    # end _analytics_start_time

    def _update_analytics_start_time(self, start_time):
        col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
        col_family.insert(SYSTEM_OBJECT_ANALYTICS,
                {SYSTEM_OBJECT_START_TIME: start_time})
    # end _update_analytics_start_time

    def purge_old_data(self, purge_time):
        try:
            total_rows_deleted = 0 # total number of rows deleted
            table_list = self._sysm.get_keyspace_column_families(COLLECTOR_KEYSPACE)
            excluded_table_list = ['MessageTable', 'FlowRecordTable', 'MessageTableTimestamp', 'SystemObjectTable']
            for table in table_list:
                # purge from index tables
                if (table not in excluded_table_list):
                    self._logger.info("deleting old records from table: %s" % str(table))
                    per_table_deleted = 0 # total number of rows deleted from each table
                    cf = pycassa.ColumnFamily(self._pool, table)
                    b = cf.batch()
                    for key, cols in cf.get_range():
                        t2 = key[0]
                        # each row will have equivalent of 2^23 = 8388608 usecs
                        row_time = (float(t2)*pow(2, RowTimeInBits))
                        if (row_time < purge_time):
                            per_table_deleted +=1
                            total_rows_deleted +=1
                            b.remove(key)
                        b.send()
                    self._logger.info("deleted %s rows from table : %s" % (str(per_table_deleted), table))
            self._logger.info("total rows deleted: %s" % str(total_rows_deleted))
            return total_rows_deleted
        except Exception as e:
            self._logger.error("Exception: %s" % e)
    # end purge_old_data    
                
    def db_purge(self, purge_input):
        if (purge_input != None):
            current_time = UTCTimestampUsec()
            analytics_start_time = float(self._analytics_start_time())
            purge_time = analytics_start_time + (float((purge_input)*(float(current_time) - analytics_start_time)))/100
            total_rows_deleted = self.purge_old_data(purge_time)
            self._update_analytics_start_time(int(purge_time))
        return total_rows_deleted
    # end db_purge
