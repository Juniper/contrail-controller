#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import requests
import ConfigParser
import argparse
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

def parse_args(args_str=None):
    defaults = {
        'old_rabbit_user': 'guest',
        'old_rabbit_password': 'guest',
        'old_rabbit_ha_mode': False,
        'old_rabbit_q_name' : 'vnc-config.issu-queue',
        'old_rabbit_vhost' : None,
        'old_rabbit_port' : '5672',
        'new_rabbit_user': 'guest',
        'new_rabbit_password': 'guest',
        'new_rabbit_ha_mode': False,
        'new_rabbit_q_name': 'vnc-config.issu-queue',
        'new_rabbit_vhost' : '/v2',
        'new_rabbit_port': '5672',
        'odb_prefix' : '',
        'ndb_prefix': 'v2',
        'reset_config': None,
        'old_cassandra_address_list': '10.84.24.35:9160',
        'old_zookeeper_address_list': '10.84.24.35:2181',
        'old_rabbit_address_list': '10.84.24.35',
        'new_cassandra_address_list': '10.84.24.35:9160',
        'new_zookeeper_address_list': '10.84.24.35:2181',
        'new_rabbit_address_list': '10.84.24.35',
        'new_api_info' : '{"10.84.24.52": [("root"), ("c0ntrail123")]}'

    }
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    conf_parser = argparse.ArgumentParser(add_help=False)
    conf_parser.add_argument("-c", "--conf_file", action='append',
            help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())
    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.set_defaults(**defaults)
    parser.add_argument(
        "--old_rabbit_user",
        help="Old RMQ user name")
    parser.add_argument(
        "--old_rabbit_password",
        help="Old RMQ passwd")
    parser.add_argument(
        "--old_rabbit_ha_mode",
        help="Old RMQ HA mode")
    parser.add_argument(
        "--old_rabbit_q_name",
        help="Q name in old RMQ")
    parser.add_argument(
        "--old_rabbit_vhost",
        help="Old RMQ Vhost")
    parser.add_argument(
        "--old_rabbit_port",
        help="Old RMQ port")
    parser.add_argument(
        "--new_rabbit_user",
        help="New RMQ user name")
    parser.add_argument(
        "--new_rabbit_password",
        help="New RMQ passwd")
    parser.add_argument(
        "--new_rabbit_ha_mode",
        help="New RMQ HA mode")
    parser.add_argument(
        "--new_rabbit_q_name",
        help="Q name in new RMQ")
    parser.add_argument(
        "--new_rabbit_vhost",
        help="New RMQ Vhost")
    parser.add_argument(
        "--new_rabbit_port",
        help="New RMQ port")
    parser.add_argument(
        "--old_rabbit_address_list",
        help="Old RMQ addresses")
    parser.add_argument(
        "--old_cassandra_address_list",
        help="Old Cassandra addresses",
        nargs='+')
    parser.add_argument(
        "--old_zookeeper_address_list",
        help="Old zookeeper addresses")
    parser.add_argument(
        "--new_rabbit_address_list",
        help="New RMQ addresses")
    parser.add_argument(
        "--new_cassandra_address_list",
        help="New Cassandra addresses",
        nargs='+')
    parser.add_argument(
        "--new_zookeeper_address_list",
        help="New zookeeper addresses")
    parser.add_argument(
        "--old_db_prefix",
        help="Old DB prefix")
    parser.add_argument(
        "--new_db_prefix",
        help="New DB prefix")
    parser.add_argument(
        "--reset_config",
        help="Reset config")
    parser.add_argument(
        "--new_api_info",
        help="New API info",
        nargs="+")
    args_obj, remaining_argv = parser.parse_known_args(remaining_argv)
    if args.conf_file:
        args_obj.config_sections = config
    if type(args_obj.old_cassandra_address_list) is str:
        args_obj.old_cassandra_address_list=\
            args_obj.old_cassandra_address_list.split()

    if type(args_obj.new_cassandra_address_list) is str:
        args_obj.new_cassandra_address_list=\
            args_obj.new_cassandra_address_list.split()

    return args_obj, remaining_argv

if __name__ == '__main__':
    parse_args()
