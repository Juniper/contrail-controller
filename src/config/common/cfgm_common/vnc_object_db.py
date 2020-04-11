#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of object db
"""
from __future__ import absolute_import
from __future__ import unicode_literals

from builtins import object
from . import vnc_cassandra

class VncObjectDBClient(object):
    def __init__(self, server_list=None, db_prefix=None, rw_keyspaces=None,
                 ro_keyspaces=None, logger=None, generate_url=None,
                 reset_config=False, credential=None, walk=True,
                 obj_cache_entries=0, obj_cache_exclude_types=None,
                 debug_obj_cache_types=None, connection=None,
                 db_engine='cassandra', ssl_enabled=False, ca_certs=None):
            if db_engine == 'cassandra':
                self._object_db = vnc_cassandra.VncCassandraClient(
                    server_list,
                    db_prefix=db_prefix,
                    rw_keyspaces=rw_keyspaces,
                    ro_keyspaces=ro_keyspaces,
                    logger=logger,
                    generate_url=generate_url,
                    reset_config=reset_config,
                    credential=credential,
                    walk=walk,
                    obj_cache_entries=obj_cache_entries,
                    obj_cache_exclude_types=obj_cache_exclude_types,
                    debug_obj_cache_types=debug_obj_cache_types,
                    ssl_enabled=ssl_enabled,
                    ca_certs=ca_certs)
            else:
                msg = ("Contrail API server does not support database backend "
                       "'%s'" % db_engine)
                raise NotImplementedError(msg)

    def __getattr__(self, name):
        return getattr(self._object_db, name)
