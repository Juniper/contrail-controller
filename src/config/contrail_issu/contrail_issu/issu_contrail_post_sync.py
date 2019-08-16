#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from __future__ import absolute_import
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from .issu_contrail_common import ICCassandraClient
import logging
from . import issu_contrail_config


def _myprint(x, level):
    print(x)
    logging.info(x)

def _issu_cassandra_post_sync_main():

    logging.basicConfig(
        level=logging.INFO,
        filename='/var/log/contrail/issu_contrail_post_sync.log',
        format='%(asctime)s %(message)s')

    args, remaining_args = issu_contrail_config.parse_args()
    issu_cass_post = ICCassandraClient(
        args.old_cassandra_address_list,
        args.new_cassandra_address_list,
        args.old_cassandra_user, args.old_cassandra_password,
        args.new_cassandra_user, args.new_cassandra_password,
        args.old_cassandra_use_ssl, args.old_cassandra_ca_certs,
        args.new_cassandra_use_ssl, args.new_cassandra_ca_certs,
        args.odb_prefix, args.ndb_prefix,
        issu_contrail_config.issu_info_post, issu_contrail_config.logger)
    issu_cass_post.issu_merge_copy(
        issu_contrail_config.issu_keyspace_to_bgp_keyspace)
    issu_contrail_config.lognprint("Done syncing bgp keyspace",
                                   level=SandeshLevel.SYS_INFO)
    issu_cass_post.issu_merge_copy(
        issu_contrail_config.issu_keyspace_user_agent)
    issu_contrail_config.lognprint("Done syncing useragent keyspace",
                                   level=SandeshLevel.SYS_INFO)
    issu_cass_post.issu_merge_copy(
        issu_contrail_config.issu_keyspace_svc_monitor_keyspace)
    issu_contrail_config.lognprint("Done syncing svc-monitor keyspace",
                                   level=SandeshLevel.SYS_INFO)
    issu_cass_post.issu_merge_copy(issu_contrail_config.issu_keyspace_dm_keyspace)
    issu_contrail_config.lognprint("Done syncing dm keyspace",
                                   level=SandeshLevel.SYS_INFO)

if __name__ == "__main__":
    _issu_cassandra_post_sync_main()
