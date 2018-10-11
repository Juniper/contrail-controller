# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Kube network manager DB
"""

from cfgm_common.vnc_object_db import VncObjectDBClient, VncObjectEtcdClient
from cfgm_common.vnc_etcd import etcd_args

DRIVER_CASS = 'cassandra'
DRIVER_ETCD = 'etcd'


class KubeNetworkManagerDB(object):
    def __init__(self, args, logger):
        self._db_logger = logger
        self._db_logger.log("KubeNetworkManagerDB arguments: {}".format(args))

        if not hasattr(args, 'db_driver') or not args.db_driver:
            msg = str("Contrail API server supports cassandra and etcd "
                      "backends, but neither of them have been configured.")
            raise ValueError(msg)

        if args.db_driver == DRIVER_ETCD:
            self._object_db = self._etcd_driver(args)
        else:
            self._object_db = self._cass_driver(args)

    def __getattr__(self, name):
        return getattr(self._object_db, name)

    def _cass_driver(self, args):
        vnc_db = {
            'server_list': args.cassandra_server_list,
            'db_prefix': args.cluster_id,
            'logger': self._db_logger.log,
            'reset_config': False,
            'credential': None,
        }
        if args.cassandra_user and args.cassandra_password:
            vnc_db['credential'] = {'username': args.cassandra_user,
                                    'password': args.cassandra_password}
        self._db_logger.log("VncObjectDBClient arguments: {}".format(vnc_db))
        return VncObjectDBClient(**vnc_db)

    def _etcd_driver(self, args):
        vnc_db = etcd_args(args)
        self._db_logger.log("VncObjectEtcdClient arguments: {}".format(vnc_db))
        return VncObjectEtcdClient(logger=self._db_logger.log, **vnc_db)
