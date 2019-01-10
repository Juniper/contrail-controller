# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from cassandra import SchemaTransformerDB
from etcd import SchemaTransformerEtcd

def schema_transformer_db_factory(args, vnc_lib, zookeeper_client, logger):
    """SchemaTransformerDB factory function"""

    if hasattr(args, "db_driver") and args.db_driver == "etcd":
        # Initialize etcd
        return SchemaTransformerEtcd(args, vnc_lib, logger)

    # Initialize cassandra
    return SchemaTransformerDB(args, zookeeper_client, logger)
