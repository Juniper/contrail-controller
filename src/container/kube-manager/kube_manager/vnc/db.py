# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Kube network manager DB
"""

from cfgm_common.vnc_object_db import VncObjectDBClient, VncObjectEtcdClient


class KubeNetworkManagerDB(object):
    def __init__(self, args, logger):
        self._db_logger = logger
        if args.cassandra_server_list and not args.etcd_server:
            self._cass_backend(args)
        elif args.etcd_server and not args.cassandra_server_list:
            self._etcd_backend(args)
        else:
            msg = str("Contrail API server supports cassandra and etcd "
                      "backends, but neither of them have been configured.")
            raise NotImplementedError(msg)

    def __getattr__(self, name):
        return getattr(self._object_db, name)

    def _cass_backend(self, args):
        vnc_db = {
            'server_list': args.cassandra_server_list,
            'db_prefix': args.cluster_id,
            'logger': self._db_logger.log,
            'reset_config': False,
        }
        if args.cassandra_user and args.cassandra_password:
            vnc_db['credential'] = {'username': args.cassandra_user,
                                    'password': args.cassandra_password}
        self._object_db = VncObjectDBClient(**vnc_db)

    def _etcd_backend(self, args):
        server = args.etcd_server.split(':')
        vnc_db = {
            'host': server[0],
            'port': server[1],
            'logger': self._db_logger.log,
        }
        if args.etcd_user and args.etcd_password:
            vnc_db['credential'] = {'user': args.etcd_user,
                                    'password': args.etcd_password}
        self._object_db = VncObjectEtcdClient(**vnc_db)
