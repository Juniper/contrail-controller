#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of object db
"""

import vnc_cassandra
import vnc_rdbms

class VncObjectDBClient(object):
    def __init__(self, server_list=None, db_prefix=None, rw_keyspaces=None, ro_keyspaces=None,
            logger=None, generate_url=None, reset_config=False, credential=None,
            walk=True, obj_cache_entries=0, obj_cache_exclude_types=None,
            connection=None, db_engine='cassandra', ssl_enabled=False,
            ca_certs=None):
            if db_engine == 'cassandra':
                self._object_db = vnc_cassandra.VncCassandraClient(server_list, db_prefix, rw_keyspaces,
                    ro_keyspaces, logger, generate_url, reset_config, credential, walk, obj_cache_entries,
                    obj_cache_exclude_types, None, ssl_enabled, ca_certs)
            elif db_engine == 'rdbms':
                self._object_db = vnc_rdbms.VncRDBMSClient(
                    server_list, db_prefix, logger, generate_url,
                    connection, reset_config, credential, obj_cache_entries)

    def __getattr__(self, name):
        return getattr(self._object_db, name)
