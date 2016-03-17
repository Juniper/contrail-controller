#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.vnc_cassandra import VncCassandraClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from issu_contrail_common import ICCassandraClient
from issu_contrail_common import ICCassandraInfo
import logging
import issu_contrail_config


def _myprint(x, level):
    print x
    logging.info(x)

def _issu_cassandra_post_sync_main():

    logging.basicConfig(
        level=logging.INFO,
        filename='/var/log/issu_contrail_post_sync.log',
        format='%(asctime)s %(message)s')

    args, remaining_args = issu_contrail_config.parse_args()
    issu_cass_post = ICCassandraClient(
        args.old_cassandra_address_list,
        args.new_cassandra_address_list,
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
