# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor DB to store VM, SI information
"""
import inspect

from cfgm_common import jsonutils as json
from cfgm_common.vnc_object_db import VncObjectDBClient
from sandesh_common.vns.constants import SVC_MONITOR_KEYSPACE_NAME

class ServiceMonitorDB(VncObjectDBClient):

    def __init__(self, args, logger):
        self._db_logger = logger
        db_engine = args.db_engine
        db_server_list = None
        cred = None
        if db_engine == 'cassandra':
            db_server_list = args.cassandra_server_list
            if (args.cassandra_user is not None and
                args.cassandra_password is not None):
                cred={'username':args.cassandra_user,
                    'password':args.cassandra_password}
        if db_engine == 'rdbms':
            db_server_list = args.rdbms_server_list
            if (args.rdbms_user is not None and
                args.rdbms_password is not None):
                cred={'username':args.rdbms_user,
                    'password':args.rdbms_password}

        super(ServiceMonitorDB, self).__init__(db_server_list,
                                               args.cluster_id,
                                               None,
                                               None,
                                               self._db_logger.log,
                                               reset_config=args.reset_config,
                                               credential=cred,
                                               connection=args.rdbms_connection,
                                               db_engine=args.db_engine)
