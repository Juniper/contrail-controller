#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db.py
#
#

from sandesh.viz.constants import *
import json
from opserver_util import OpServerUtils

class AnalyticsDb(object):
    def __init__(self, logger, admin_port, admin_user, admin_password):
        self._logger = logger
        self._ip = '127.0.0.1'
        self._admin_port = admin_port
        self._admin_user = admin_user
        self._admin_password = admin_password
    # end __init__
    
    def get_dbusage_info(self):
        """Collects database usage information from all db nodes
        Returns:
        A dictionary with db node name as key and db usage in % as value
        """

        to_return = {}
        try:
            uve_url = "http://" + self._ip + ":" + str(self._admin_port) + \
                "/analytics/uves/database-nodes?cfilt=DatabaseUsageInfo"
            data = OpServerUtils.get_url_http(uve_url, self._admin_user,
                    self._admin_password)
            node_dburls = json.loads(data.text)

            for node_dburl in node_dburls:
                # calculate disk usage percentage for analytics in each
                # cassandra node
                db_uve_data = OpServerUtils.get_url_http(node_dburl['href'],
                    self._admin_user, self._admin_password)
                db_uve_state = json.loads(db_uve_data.text)
                db_usage = db_uve_state['DatabaseUsageInfo']['database_usage'][0]
                db_usage_in_perc = (100*
                    float(db_usage['analytics_db_size_1k'])/
                    float(db_usage['disk_space_available_1k'] +
                    db_usage['disk_space_used_1k']))
                to_return[node_dburl['name']] = db_usage_in_perc
        except Exception as inst:
            self._logger.error(type(inst))  # the exception instance
            self._logger.error(inst.args)   # arguments stored in .args
            self._logger.error(inst)        # __str__ allows args to be printed directly
            self._logger.error("Could not retrieve db usage information")

        self._logger.info("db usage:" + str(to_return))
        return to_return
    #end get_dbusage_info

    def get_pending_compaction_tasks(self):
        """Collects pending compaction tasks from all db nodes
        Returns:
        A dictionary with db node name as key and pending compaction
        tasks in % as value
        """

        to_return = {}
        try:
            uve_url = "http://" + self._ip + ":" + str(self._admin_port) + \
                "/analytics/uves/database-nodes?cfilt=" \
                "CassandraStatusData:cassandra_compaction_task"
            data = OpServerUtils.get_url_http(uve_url, self._admin_user,
                    self._admin_password)
            node_dburls = json.loads(data.text)

            for node_dburl in node_dburls:
                # get pending compaction tasks for analytics in each
                # cassandra node
                db_uve_data = OpServerUtils.get_url_http(node_dburl['href'],
                    self._admin_user, self._admin_password)
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
