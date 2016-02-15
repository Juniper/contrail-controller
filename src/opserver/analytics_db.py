#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db.py
# Implementation of database purging
#

import redis
import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily
from pycassa.types import *
from pycassa import *
from sandesh.viz.constants import *
from sandesh.viz.constants import _NO_AUTO_PURGE_TABLES, \
        _FLOW_TABLES, _STATS_TABLES, _MSG_TABLES
from pysandesh.util import UTCTimestampUsec
import code
import urllib2
import time
import json
import datetime
import pdb
import argparse
import socket
import struct

class AnalyticsDb(object):
    def __init__(self, logger, cassandra_server_list,
                    redis_query_port, redis_password):
        self._logger = logger
        self._cassandra_server_list = cassandra_server_list
        self._redis_query_port = redis_query_port
        self._redis_password = redis_password
        self._pool = None
        self.connect_db()
        self.number_of_purge_requests = 0
    # end __init__

    def connect_db(self):
        try:
            self._pool = ConnectionPool(COLLECTOR_KEYSPACE,
                         server_list=self._cassandra_server_list, timeout=None)
        except Exception as e:
            self._logger.error("Exception: Failure in connection to "
                "AnalyticsDb %s" % e)
            return -1
        return None
    # end connect_db

    def _get_sysm(self):
        for server_and_port in self._cassandra_server_list:
            try:
                sysm = pycassa.system_manager.SystemManager(server_and_port)
            except Exception as e:
                self._logger.error("Exception: SystemManager failed %s" % e)
                continue
            else:
                return sysm
        return None
    # end _get_sysm

    def _get_analytics_ttls(self):
        ret_row = {}
        try:
            col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
            row = col_family.get(SYSTEM_OBJECT_ANALYTICS)
        except Exception as e:
            self._logger.error("Exception: analytics_start_time Failure %s" % e)
            ret_row[SYSTEM_OBJECT_FLOW_DATA_TTL] = AnalyticsFlowTTL
            ret_row[SYSTEM_OBJECT_STATS_DATA_TTL] = AnalyticsStatisticsTTL
            ret_row[SYSTEM_OBJECT_CONFIG_AUDIT_TTL] = AnalyticsConfigAuditTTL
            ret_row[SYSTEM_OBJECT_GLOBAL_DATA_TTL] = AnalyticsTTL
            return ret_row

        if (SYSTEM_OBJECT_FLOW_DATA_TTL not in row):
            ret_row[SYSTEM_OBJECT_FLOW_DATA_TTL] = AnalyticsFlowTTL
        else:
            ret_row[SYSTEM_OBJECT_FLOW_DATA_TTL] = row[SYSTEM_OBJECT_FLOW_DATA_TTL]
        if (SYSTEM_OBJECT_STATS_DATA_TTL not in row):
            ret_row[SYSTEM_OBJECT_STATS_DATA_TTL] = AnalyticsStatisticsTTL
        else:
            ret_row[SYSTEM_OBJECT_STATS_DATA_TTL] = row[SYSTEM_OBJECT_STATS_DATA_TTL]
        if (SYSTEM_OBJECT_CONFIG_AUDIT_TTL not in row):
            ret_row[SYSTEM_OBJECT_CONFIG_AUDIT_TTL] = AnalyticsConfigAuditTTL
        else:
            ret_row[SYSTEM_OBJECT_CONFIG_AUDIT_TTL] = row[SYSTEM_OBJECT_CONFIG_AUDIT_TTL]
        if (SYSTEM_OBJECT_GLOBAL_DATA_TTL not in row):
            ret_row[SYSTEM_OBJECT_GLOBAL_DATA_TTL] = AnalyticsTTL
        else:
            ret_row[SYSTEM_OBJECT_GLOBAL_DATA_TTL] = row[SYSTEM_OBJECT_GLOBAL_DATA_TTL]

        return ret_row
    # end _get_analytics_ttls

    def _get_analytics_start_time(self):
        try:
            col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
            row = col_family.get(SYSTEM_OBJECT_ANALYTICS)
        except Exception as e:
            self._logger.error("Exception: analytics_start_time Failure %s" % e)
            return None

        # Initialize the dictionary before returning
        if (SYSTEM_OBJECT_START_TIME not in row):
            return None
        ret_row = {}
        ret_row[SYSTEM_OBJECT_START_TIME] = row[SYSTEM_OBJECT_START_TIME]
        if (SYSTEM_OBJECT_FLOW_START_TIME not in row):
            ret_row[SYSTEM_OBJECT_FLOW_START_TIME] = row[SYSTEM_OBJECT_START_TIME]
        else:
            ret_row[SYSTEM_OBJECT_FLOW_START_TIME] = row[SYSTEM_OBJECT_FLOW_START_TIME]
        if (SYSTEM_OBJECT_STAT_START_TIME not in row):
            ret_row[SYSTEM_OBJECT_STAT_START_TIME] = row[SYSTEM_OBJECT_START_TIME]
        else:
            ret_row[SYSTEM_OBJECT_STAT_START_TIME] = row[SYSTEM_OBJECT_STAT_START_TIME]
        if (SYSTEM_OBJECT_MSG_START_TIME not in row):
            ret_row[SYSTEM_OBJECT_MSG_START_TIME] = row[SYSTEM_OBJECT_START_TIME]
        else:
            ret_row[SYSTEM_OBJECT_MSG_START_TIME] = row[SYSTEM_OBJECT_MSG_START_TIME]

        return ret_row
    # end _get_analytics_start_time

    def _update_analytics_start_time(self, start_times):
        try:
            col_family = ColumnFamily(self._pool, SYSTEM_OBJECT_TABLE)
            col_family.insert(SYSTEM_OBJECT_ANALYTICS, start_times)
        except Exception as e:
            self._logger.error("Exception: update_analytics_start_time "
                "Connection Failure %s" % e)
    # end _update_analytics_start_time

    def set_analytics_db_purge_status(self, purge_id, purge_cutoff):
        try:
            redish = redis.StrictRedis(db=0, host='127.0.0.1',
                     port=self._redis_query_port, password=self._redis_password)
            redish.hset('ANALYTICS_DB_PURGE', 'status', 'running')
            redish.hset('ANALYTICS_DB_PURGE', 'purge_input', str(purge_cutoff))
            redish.hset('ANALYTICS_DB_PURGE', 'purge_start_time',
                        UTCTimestampUsec())
            redish.hset('ANALYTICS_DB_PURGE', 'purge_id', purge_id)
        except redis.exceptions.ConnectionError:
            self._logger.error("Exception: "
                               "Failure in connection to redis-server")
            response = {'status': 'failed',
                        'reason': 'Failure in connection to redis-server'}
            return response
        except redis.exceptions.ResponseError:
            self._logger.error("Exception: "
                               "Redis authentication failed")
            response = {'status': 'failed',
                        'reason': 'Redis authentication failed'}
            return response
        return None
    # end set_analytics_db_purge_status

    def delete_db_purge_status(self):
        try:
            redish = redis.StrictRedis(db=0, host='127.0.0.1',
                     port=self._redis_query_port, password=self._redis_password)
            redish.delete('ANALYTICS_DB_PURGE')
        except redis.exceptions.ConnectionError:
            self._logger.error("Exception: "
                               "Failure in connection to redis-server")
        except redis.exceptions.ResponseError:
            self._logger.error("Exception: "
                               "Redis authentication failed")
    # end delete_db_purge_status

    def get_analytics_db_purge_status(self, redis_list):
        for redis_ip_port in redis_list:
            try:
                redish = redis.StrictRedis(redis_ip_port[0],
                                           redis_ip_port[1], db=0,
                                           password=self._redis_password)
                if (redish.exists('ANALYTICS_DB_PURGE')):
                    return redish.hgetall('ANALYTICS_DB_PURGE')
            except redis.exceptions.ConnectionError:
                self._logger.error("Exception: "
                                   "Failure in connection to redis-server")
                response = {'status': 'failed',
                            'reason': 'Failure in connection to redis-server: '
                                       + redis_ip_port[0]}
                return response
            except redis.exceptions.ResponseError:
                self._logger.error("Exception: "
                                   "Redis authentication failed")
                response = {'status': 'failed',
                            'reason': 'Redis authentication failed'}
                return response
        return None
    # end get_analytics_db_purge_status

    def db_purge(self, purge_cutoff, purge_id):
        total_rows_deleted = 0 # total number of rows deleted
        purge_error_details = []
        if (self._pool == None):
            self.connect_db()
        if not self._pool:
            self._logger.error('Connection to AnalyticsDb has Timed out')
            purge_error_details.append('Connection to AnalyticsDb has Timed out')
            return (-1, purge_error_details)
        sysm = self._get_sysm()
        if (sysm == None):
            self._logger.error('Failed to connect SystemManager')
            purge_error_details.append('Failed to connect SystemManager')
            return (-1, purge_error_details)
        try:
            table_list = sysm.get_keyspace_column_families(COLLECTOR_KEYSPACE)
        except Exception as e:
            self._logger.error("Exception: Purge_id %s Failed to get "
                "Analytics Column families %s" % (purge_id, e))
            purge_error_details.append("Exception: Failed to get "
                "Analytics Column families %s" % (e))
            return (-1, purge_error_details)

        # delete entries from message table
        msg_table = COLLECTOR_GLOBAL_TABLE
        # total number of rows deleted from this table
        msg_table_deleted = 0
        try:
            msg_cf = pycassa.ColumnFamily(self._pool, msg_table)
        except Exception as e:
            self._logger.error("purge_id %s Failure in fetching "
                "message table columnfamily %s" % e)
            purge_error_details.append("Failure in fetching "
                "message table columnfamily %s" % e)
            return (-1, purge_error_details)

        for table in table_list:
            # purge from index tables
            if (table not in _NO_AUTO_PURGE_TABLES):
                self._logger.info("purge_id %s deleting old records from "
                                  "table: %s" % (purge_id, table))

                # determine purge cutoff time
                if (table in _FLOW_TABLES):
                    purge_time = purge_cutoff['flow_cutoff']
                elif (table in _STATS_TABLES):
                    purge_time = purge_cutoff['stats_cutoff']
                elif (table in _MSG_TABLES):
                    purge_time = purge_cutoff['msg_cutoff']
                else:
                    purge_time = purge_cutoff['other_cutoff']

                del_msg_uuids = [] # list of uuids of messages to be deleted

                # total number of rows deleted from each table
                per_table_deleted = 0
                try:
                    cf = pycassa.ColumnFamily(self._pool, table)
                except Exception as e:
                    self._logger.error("purge_id %s Failure in fetching "
                        "the columnfamily %s" % e)
                    purge_error_details.append("Failure in fetching "
                        "the columnfamily %s" % e)
                    return (-1, purge_error_details)
                b = cf.batch()
                try:
                    # get all columns only in case of one message index table
                    if table == MESSAGE_TABLE_SOURCE:
                        cols_to_fetch = 1000000
                    else:
                        cols_to_fetch = 1

                    for key, cols in cf.get_range(column_count=cols_to_fetch):
                        # key is of type integer for MESSAGE_TABLE_TIMESTAMP.
                        # For other tables, key is a composite type with
                        # first element being timestamp (integer).
                        if table == MESSAGE_TABLE_TIMESTAMP:
                            t2 = key
                        else:
                            t2 = key[0]
                        # each row will have equivalent of 2^23 = 8388608 usecs
                        row_time = (float(t2)*pow(2, RowTimeInBits))
                        if (row_time < purge_time):
                            per_table_deleted +=1
                            total_rows_deleted +=1
                            if table == MESSAGE_TABLE_SOURCE:
                                # get message table uuids to delete
                                del_msg_uuids.extend(cols.values())
                            try:
                                b.remove(key)
                            except Exception as e:
                                self._logger.error("Exception: Purge_id:%s table:%s "
                                        "error: %s" % (purge_id, table, e))
                                b = cf.batch() # create a new batch job
                                continue
                    try:
                        b.send()
                    except Exception as e:
                        self._logger.error("Exception: Purge_id:%s table:%s "
                                "error: %s" % (purge_id, table, e))

                    if len(del_msg_uuids) != 0:
                        # delete uuids from the message table
                        b_msgtbl = msg_cf.batch()
                        try:
                            for key in del_msg_uuids:
                                msg_table_deleted +=1
                                total_rows_deleted +=1
                                b_msgtbl.remove(key)
                            b_msgtbl.send()
                        except Exception as e:
                            self._logger.error("Exception: Purge_id %s message table "
                                "doesnot have uuid %s" % (purge_id, e))
                            purge_error_details.append("Exception: Message table "
                                "doesnot have uuid %s" % (e))
                except Exception as e:
                    self._logger.error("Exception: Purge_id:%s table:%s "
                            "error: %s" % (purge_id, table, e))
                    purge_error_details.append("Exception: Table:%s "
                            "error: %s" % (table, e))
                    continue
                self._logger.warning("Purge_id %s deleted %d rows from "
                    "table: %s" % (purge_id, per_table_deleted, table))

        self._logger.warning("Purge_id %s deleted %d rows from table: %s"
            % (purge_id, msg_table_deleted, COLLECTOR_GLOBAL_TABLE))
        self._logger.warning("Purge_id %s total rows deleted: %s"
            % (purge_id, total_rows_deleted))
        return (total_rows_deleted, purge_error_details)
    # end db_purge

    def get_dbusage_info(self, rest_api_ip, rest_api_port):
        """Collects database usage information from all db nodes
        Returns:
        A dictionary with db node name as key and db usage in % as value
        """

        to_return = {}
        try:
            uve_url = "http://" + rest_api_ip + ":" + str(rest_api_port) + "/analytics/uves/database-nodes?cfilt=DatabaseUsageInfo"
            node_dburls = json.loads(urllib2.urlopen(uve_url).read())

            for node_dburl in node_dburls:
                # calculate disk usage percentage for analytics in each cassandra node
                db_uve_state = json.loads(urllib2.urlopen(node_dburl['href']).read())
                db_usage_in_perc = (100*
                        float(db_uve_state['DatabaseUsageInfo']['database_usage'][0]['analytics_db_size_1k'])/
                        float(db_uve_state['DatabaseUsageInfo']['database_usage'][0]['disk_space_available_1k'] +
                        db_uve_state['DatabaseUsageInfo']['database_usage'][0]['disk_space_used_1k']))
                to_return[node_dburl['name']] = db_usage_in_perc
        except Exception as inst:
            self._logger.error(type(inst))     # the exception instance
            self._logger.error(inst.args)      # arguments stored in .args
            self._logger.error(inst)           # __str__ allows args to be printed directly
            self._logger.error("Could not retrieve db usage information")

        self._logger.info("db usage:" + str(to_return))
        return to_return
    #end get_dbusage_info

# end AnalyticsDb
