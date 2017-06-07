#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db.py
# Implementation of database purging
#

import redis
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
from cassandra.cluster import Cluster
from cassandra.auth import PlainTextAuthProvider
from cassandra import ConsistencyLevel
from cassandra.query import dict_factory, named_tuple_factory
from cassandra.io.geventreactor import GeventConnection
from cassandra.query import named_tuple_factory
from cassandra.query import PreparedStatement, tuple_factory
import platform
from opserver_util import OpServerUtils
from strict_redis_wrapper import StrictRedisWrapper

class AnalyticsDb(object):
    def __init__(self, logger, cassandra_server_list,
                    redis_query_port, redis_password, cassandra_user,
                    cassandra_password, cluster_id):
        self._logger = logger
        self._cassandra_server_list = cassandra_server_list
        self._redis_query_port = redis_query_port
        self._redis_password = redis_password
        self._pool = None
        self._session = None
        self._cluster_id = cluster_id
        self._cassandra_user = cassandra_user
        self._cassandra_password = cassandra_password
        if (cluster_id != ''):
            self._keyspace = COLLECTOR_KEYSPACE_CQL + '_' + cluster_id
        else:
            self._keyspace = COLLECTOR_KEYSPACE_CQL
        self.connect_db()
        self.number_of_purge_requests = 0
    # end __init__
    
    def connect_db(self):
        self.get_cql_session()
    # end connect_db

    def _get_analytics_ttls_cql(self):
        session = self.get_cql_session()
        if not session:
            return None
        ret_row = {}
        try:
            ttl_query = "SELECT * FROM %s" % SYSTEM_OBJECT_TABLE.lower()
            session.row_factory = dict_factory
            rs = session.execute(ttl_query)
            for r in rs:
                row = r
            session.row_factory = named_tuple_factory
            return (row, 0)
        except Exception as e:
            self._logger.error("Exception: analytics_start_time Failure ")
            ret_row[SYSTEM_OBJECT_FLOW_DATA_TTL] = AnalyticsFlowTTL
            ret_row[SYSTEM_OBJECT_STATS_DATA_TTL] = AnalyticsStatisticsTTL
            ret_row[SYSTEM_OBJECT_CONFIG_AUDIT_TTL] = AnalyticsConfigAuditTTL
            ret_row[SYSTEM_OBJECT_GLOBAL_DATA_TTL] = AnalyticsTTL
            return (ret_row, -1)
    # end _get_analytics_ttls_cql
    
    def get_analytics_ttls(self):
        ret_row = {}
        (row, status) = self._get_analytics_ttls_cql()

        if status == -1:
            return row

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
    # end get_analytics_ttls

    def _get_analytics_start_time_cql(self):
        session = self.get_cql_session()
        if not session:
            return None
        # old_row_factory is usually named_tuple_factory
        old_row_factory = session.row_factory
        session.row_factory = dict_factory
        try:
            start_time_query = "SELECT * FROM %s" % \
                (SYSTEM_OBJECT_TABLE)
            rs = session.execute(start_time_query)
            for r in rs:
                row = r
            session.row_factory = old_row_factory
            return row
        except Exception as e:
            self._logger.error("Exception: analytics_start_time Failure %s" % e)
            return None
    # end _get_analytics_start_time_cql
   
    def get_analytics_start_time(self):
        row = self._get_analytics_start_time_cql()

        if row is None:
            return row
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
    # end get_analytics_start_time

    def get_cql_session(self):
        if self._session:
            return self._session
        creds=None
        try:
            if self._cassandra_user is not None and \
               self._cassandra_password is not None:
                   creds = PlainTextAuthProvider(username=self._cassandra_user,
                       password=self._cassandra_password)
            # Extract the server_list and port number seperately
            server_list = [i.split(":")[0] for i in self._cassandra_server_list]
            cql_port = int(self._cassandra_server_list[0].split(":")[1])
            cluster = Cluster(contact_points = server_list,
                auth_provider = creds, port = cql_port)
            self._session=cluster.connect(self._keyspace)
            self._session.connection_class = GeventConnection
            self._session.default_consistency_level = ConsistencyLevel.LOCAL_ONE
            return self._session
        except Exception as e:
            self._logger.error("Exception: get_cql_session Failure %s" % e)
            return None
        # end get_cql_session

    def _update_analytics_start_time_cql(self, start_times):
        # The column names in SYSTEM_OBJECT_TABLE have to be encoded in ""
        key_subst = 'key, ' + ', '.join('"' + item + '"' for item in \
            start_times.keys())
        # The SYSTEM_OBJECT_ANALYTICS col name has to be inside ''
        val_subst = '\''+ SYSTEM_OBJECT_ANALYTICS + '\', '
        val_subst += ",".join(map(str,start_times.values()))
        insert_query = "INSERT INTO %s (%s) VALUES (%s) " % \
            (SYSTEM_OBJECT_TABLE, key_subst, val_subst)
        try:
            self._session.execute(insert_query)
        except Exception as e:
            self._logger.error("Exception: update_analytics_start_time "
                "Connection Failure %s" % e)
    
    def _update_analytics_start_time(self, start_times):
        self._update_analytics_start_time_cql(start_times)
    # end _update_analytics_start_time


    def get_dbusage_info(self, ip, port, user, password):
        """Collects database usage information from all db nodes
        Returns:
        A dictionary with db node name as key and db usage in % as value
        """

        to_return = {}
        try:
            uve_url = "http://" + ip + ":" + str(port) + \
                "/analytics/uves/database-nodes?cfilt=DatabaseUsageInfo"
            data = OpServerUtils.get_url_http(uve_url, user, password)
            node_dburls = json.loads(data.text)

            for node_dburl in node_dburls:
                # calculate disk usage percentage for analytics in each
                # cassandra node
                db_uve_data = OpServerUtils.get_url_http(node_dburl['href'],
                    user, password)
                db_uve_state = json.loads(db_uve_data.text)
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

    def get_pending_compaction_tasks(self, ip, port, user, password):
        """Collects pending compaction tasks from all db nodes
        Returns:
        A dictionary with db node name as key and pending compaction
        tasks in % as value
        """

        to_return = {}
        try:
            uve_url = "http://" + ip + ":" + str(port) + \
                "/analytics/uves/database-nodes?cfilt=" \
                "CassandraStatusData:cassandra_compaction_task"
            data = OpServerUtils.get_url_http(uve_url, user, password)
            node_dburls = json.loads(data.text)

            for node_dburl in node_dburls:
                # get pending compaction tasks for analytics in each
                # cassandra node
                db_uve_data = OpServerUtils.get_url_http(node_dburl['href'],
                    user, password)
                db_uve_state = json.loads(db_uve_data.text)
                pending_compaction_tasks = \
                    int(db_uve_state['CassandraStatusData']
                        ['cassandra_compaction_task']
                        ['pending_compaction_tasks'])
                to_return[node_dburl['name']] = pending_compaction_tasks

        except Exception as inst:
            self._logger.error("Exception: Could not retrieve pending"
                               " compaction tasks information %s" %
                               str(type(inst)))

        self._logger.info("pending compaction tasks :" + str(to_return))
        return to_return
    #end get_pending_compaction_tasks

# end AnalyticsDb
