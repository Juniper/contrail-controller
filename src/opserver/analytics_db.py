#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db.py
# Implementation of database purging
#

import redis
from sandesh.viz.constants import *
import json
from opserver_util import OpServerUtils

class AnalyticsDb(object):
    def __init__(self, logger, redis_query_port, redis_password):
        self._logger = logger
        self._redis_query_port = redis_query_port
        self._redis_password = redis_password
    # end __init__
    
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
