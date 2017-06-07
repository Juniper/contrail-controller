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
from cassandra.query import PreparedStatement
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
