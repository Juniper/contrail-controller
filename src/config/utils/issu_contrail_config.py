#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import logging
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

def _myprint(x, level):
    prefix = SandeshLevel._VALUES_TO_NAMES[level] + " "
    logging.info(prefix + str(x))

def lognprint(x, level):
    print x
    prefix = SandeshLevel._VALUES_TO_NAMES[level] + " "
    logging.info(prefix + str(x))

logger = _myprint

#Apps register respective translation functions and import paths

issu_keyspace_config_db_uuid = {
    'config_db_uuid': [
        ('obj_uuid_table'), ('obj_fq_name_table'), ('obj_shared_table')]}

issu_info_pre = [
    (None, 'config_db_uuid', [
        ('obj_uuid_table', None),
        ('obj_fq_name_table', None),
        ('obj_shared_table', None)]),
    (None, 'to_bgp_keyspace', [
        ('route_target_table', None), ('service_chain_table', None),
        ('service_chain_ip_address_table', None),
        ('service_chain_uuid_table', None)]),
    (None, 'useragent', [('useragent_keyval_table', None)]),
    (None, 'svc_monitor_keyspace', [
        ('pool_table', None), ('service_instance_table', None)]),
    (None, 'dm_keyspace', [
        ('dm_pr_vn_ip_table', None), ('dm_pnf_resource_table', None)])]

issu_keyspace_to_bgp_keyspace = {
    'to_bgp_keyspace': [
        ('route_target_table'), ('service_chain_table'),
        ('service_chain_ip_address_table'), ('service_chain_uuid_table')]}

issu_keyspace_user_agent = {'useragent': [('useragent_keyval_table')]}

issu_keyspace_svc_monitor_keyspace = {
    'svc_monitor_keyspace': [('pool_table'), ('service_instance_table')]}

issu_keyspace_dm_keyspace = {
    'dm_keyspace': [('dm_pr_vn_ip_table'), ('dm_pnf_resource_table')]}

issu_info_post = [
    (None, 'to_bgp_keyspace', [
        ('route_target_table', None), ('service_chain_table', None),
        ('service_chain_ip_address_table', None),
        ('service_chain_uuid_table', None)]),
    (None, 'useragent', [('useragent_keyval_table', None)]),
    (None, 'svc_monitor_keyspace', [
        ('pool_table', None), ('service_instance_table', None)]),
    (None, 'dm_keyspace', [
        ('dm_pr_vn_ip_table', None), ('dm_pnf_resource_table', None)])]

issu_info_config_db_uuid = [
    (None, 'config_db_uuid', [
        ('obj_uuid_table', None),
        ('obj_fq_name_table', None), ('obj_shared_table', None)])]

issu_znode_list = ['fq-name-to-uuid', 'api-server', 'id']

#The below fields will be moved to /etc/contrail/contrail_issu.conf by
#provisioning scripts. Application changes will be done then.

Cassandra_OldVersion_Address = [
    "10.84.24.35:9160"]

Cassandra_NewVersion_Address = [
    "10.84.24.35:9160"]

Zookeeper_OldVersion_Address = '10.84.24.35:2181'

Zookeeper_NewVersion_Address = '10.84.24.35:2181'

reset_config = None

odb_prefix = ''

ndb_prefix = 'v2'

old_rabbit_ip = '10.84.24.35'

old_rabbit_port = '5672'

old_rabbit_user = 'guest'

old_rabbit_password = 'guest'

old_rabbit_vhost = None

old_rabbit_ha_mode = False

old_rabbit_q_name = "vnc-config.issu-queue"

new_rabbit_ip = '10.84.24.35'

new_rabbit_port = '5672'

new_rabbit_user = 'guest'

new_rabbit_password = 'guest'

new_rabbit_vhost = '/v2'

new_rabbit_ha_mode = False

new_rabbit_q_name = "vnc-config.issu-queue"

new_api_info = {
    "10.84.24.52": [('root'), ('c0ntrail123')]}
