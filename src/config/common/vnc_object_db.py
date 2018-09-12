#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of object db
"""

import vnc_cassandra
import vnc_etcd


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
                    db_prefix,
                    rw_keyspaces,
                    ro_keyspaces,
                    logger,
                    generate_url,
                    reset_config,
                    credential,
                    walk,
                    obj_cache_entries,
                    obj_cache_exclude_types,
                    debug_obj_cache_types,
                    None,
                    ssl_enabled,
                    ca_certs,
                )
            else:
                msg = ("Contrail API server does not support database backend "
                       "'%s'" % db_engine)
                raise NotImplementedError(msg)

    def __getattr__(self, name):
        return getattr(self._object_db, name)


class VncObjectEtcdClient(object):
    def __init__(self, server, prefix, logger,
                 credential, ssl_enabled, ca_certs):
        server = server.split(':')
        self._object_db = vnc_etcd.VncEtcd(
            host=server[0], port=server[1], prefix=prefix, logger=logger,
            obj_cache_exclude_types=[],
            credential=credential, ssl_enabled=ssl_enabled, ca_certs=ca_certs)

    def __getattr__(self, name):
        if not hasattr(self._object_db, name):
            msg = ("VNC ETCD does not implement method '%s'" % name)
            raise NotImplementedError(msg)
        return getattr(self._object_db, name)
