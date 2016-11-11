#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of object db
"""
from cfgm_common.zkclient import IndexAllocator

import vnc_cassandra
import vnc_rdbms

class VncObjectDBClient(object):
    def __init__(self, server_list=None, db_prefix=None, rw_keyspaces=None, ro_keyspaces=None,
            logger=None, generate_url=None, reset_config=False, credential=None,
            walk=True, obj_cache_entries=0, obj_cache_exclude_types=None,
            connection=None, db_engine='cassandra'):
            if db_engine == None:
                db_engine = 'cassandra'
            self.db_engine = db_engine
            if db_engine == 'cassandra':
                self._allocator_class = IndexAllocator
                self._object_db = vnc_cassandra.VncCassandraClient(server_list, db_prefix, rw_keyspaces,
                    ro_keyspaces, logger, generate_url, reset_config, credential, walk, obj_cache_entries,
                    obj_cache_exclude_types)
            elif db_engine == 'rdbms':
                self._allocator_class = vnc_rdbms.RDBMSIndexAllocator
                self._object_db = vnc_rdbms.VncRDBMSClient(
                    server_list, db_prefix, logger, generate_url,
                    connection, reset_config, credential, obj_cache_entries)
            else:
                raise NotImplementedError(db_engine)

    def create_index_allocator(self, db, path, size=0, start_idx=0,
                 reverse=False,alloc_list=None, max_alloc=0):
        return self._allocator_class(db, path, size, start_idx, reverse,
            alloc_list, max_alloc)

    def __getattr__(self, name):
        if name == "_object_db":
            return None
        return getattr(self._object_db, name)