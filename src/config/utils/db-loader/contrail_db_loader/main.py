# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
from builtins import object
import argparse
from collections import OrderedDict
import logging
import sys
import yaml

from cfgm_common.cassandra import api as cassa_api
from cfgm_common.vnc_object_db import VncObjectDBClient
from cfgm_common.importutils import import_object
from keystoneclient import session as ksession, auth as kauth,\
    client as kclient, exceptions as kexceptions

from .utils import prompt, camel_case
from .zookeeper import ZookeeperClient, DummyZookeeperClient


logger = logging.getLogger(__name__)


class LoadDataBase(object):
    # Resources supported by that script
    # The order of that list is import, that defines the resources
    # order creation
    _SUPPORTED_RESOURCES = [
        'project',
        'security-group',
        'virtual-network',
        'virtual-machine-interface',
    ]
    _PERMS2 = {
        'owner': None,
        'owner_access': 7,
        'global_access': 0,
        'share': [],
    }

    BATCH_QUEUE_SIZE = 1000
    RULES_PER_SG = 4

    def __init__(self, force, resources_file, cassandra_servers,
                 cassandra_username, cassandra_password,
                 cassandra_use_ssl, cassandra_ca_certs, db_prefix,
                 cassandra_batch_size, zookeeper_servers,
                 rules_per_security_group, keystone_client,
                 dont_populate_zookeeper):
        self._force = force
        self._resource_distribution = yaml.load(resources_file)
        self._cassandra_batch_size = cassandra_batch_size
        self._rules_per_security_group = rules_per_security_group
        self._keystone_client = keystone_client
        self._dont_populate_zookeeper = dont_populate_zookeeper

        # Connect to cassandra database
        logger.debug("Initilizing the cassandra connection on %s",
                     cassandra_servers)
        cassandra_credentials = {}
        if (cassandra_username is not None and
                cassandra_password is not None):
            cassandra_credentials = {
                'username': cassandra_username,
                'password': cassandra_password,
            }

        def vnc_cassandra_client_logger(msg, level=logging.INFO):
            logger.log(msg=msg, level=level)

        self._object_db = VncObjectDBClient(
            cassandra_servers,
            db_prefix,
            cassa_api.UUID_KEYSPACE,
            None,
            vnc_cassandra_client_logger,
            credential=cassandra_credentials,
            ssl_enabled=cassandra_use_ssl,
            ca_certs=cassandra_ca_certs)
        self._uuid_cf = self._object_db.get_cf('obj_uuid_table')
        self._fqname_cf = self._object_db.get_cf('obj_fq_name_table')

        # Initilize zookeeper client
        if self._dont_populate_zookeeper:
            self._zk_client = DummyZookeeperClient()
        else:
            self._zk_client = ZookeeperClient(zookeeper_servers)

    def sanitize_resources(self):
        logger.debug("Santizing resources distribution")
        self._resource_map = OrderedDict()
        for resource_type in self._SUPPORTED_RESOURCES:
            object_path = 'contrail_db_loader.resources.%s.%s' %\
                          (resource_type.replace('-', '_'),
                           camel_case(resource_type))
            kwargs = {
                'db_manager': self._object_db,
                'batch_size': self._cassandra_batch_size,
                'zk_client': self._zk_client,
                'project_amount': self._resource_distribution.get('project',
                                                                  0),
                'amount_per_project': self._resource_distribution.get(
                    resource_type, 0),
            }
            self._resource_map[resource_type] = import_object(object_path,
                                                              **kwargs)

        resources_not_supported = (set(self._resource_distribution.keys()) -
                                   set(self._SUPPORTED_RESOURCES))
        if resources_not_supported:
            logger.warning('Loading resources %s are not supported' %
                           ', '.join(resources_not_supported))

    def summarize_resources_to_create(self):
        msg = """Will populate %(project)d projects with:
    - security groups:           %(sg)d
    - access control lists:      %(acl)d
    - virtual networks:          %(vn)d
    - routing instances:         %(ri)d
    - route targets:             %(rt)d
    - virtual machine interface: %(vmi)d
    - virtual machine:           %(vm)d
    - intance ip:                %(iip)d
That will load %(sum)d resources into database."""
        dict = {
            'project': self._resource_map['project'].total_amount,
            'sg': self._resource_map['security-group'].amount_per_project + 1,
            'acl': (self._resource_map['security-group'].amount_per_project +
                    1) * 2,
            'vn': self._resource_map['virtual-network'].amount_per_project,
            'ri': self._resource_map['virtual-network'].amount_per_project,
            'rt': self._resource_map['virtual-network'].amount_per_project,
            'vmi': self._resource_map['virtual-machine-interface'].
            amount_per_project,
            'vm': self._resource_map['virtual-machine-interface'].
            amount_per_project,
            'iip': self._resource_map['virtual-machine-interface'].
            amount_per_project,
        }
        dict['sum'] = 0
        for resource in list(self._resource_map.values()):
            dict['sum'] += resource.total_amount
        logger.warning(msg, dict)
        if (not self._force and
                not prompt('Do you want to load that amount of resources?')):
            exit(0)

    def create_resources(self):
        self._zk_client.connect()
        for resource in list(self._resource_map.values()):
            logger.info("Loading '%s' resources into the database...",
                        resource.type)
            if resource.type == 'project':
                _, time_elapsed = resource.create_resources(
                    self._keystone_client)
            elif resource.type == 'security-group':
                _, time_elapsed = resource.create_resources(
                    self._rules_per_security_group)
            elif resource.type == 'virtual-machine-interface':
                _, time_elapsed = resource.create_resources(
                    self._resource_map['virtual-network'].amount_per_project,
                    self._resource_map['security-group'].amount_per_project)
            else:
                _, time_elapsed = resource.create_resources()
            logger.info("%d resources were created to load %d '%s' in "
                        "%2.2f seconds.", resource.total_amount,
                        resource.amount_per_project, resource.type,
                        time_elapsed)
        self._zk_client.disconnect()


def main():
    argv = sys.argv[1:]

    if '-d' in argv or '--debug' in argv:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(
        description="Command to generate load on database")
    parser.add_argument('-f', '--force', action='store_true', default=False)
    parser.add_argument('--resources-file',
                        required=True,
                        help='YAML file describing resources to load',
                        type=argparse.FileType('r'))
    parser.add_argument('--cassandra-servers',
                        help="Cassandra server list' (default: %(default)s)",
                        nargs='+',
                        default=['localhost:9160'])
    parser.add_argument('--cassandra-username',
                        help="Cassandra user name (default: %(default)s)",
                        default=None)
    parser.add_argument('--cassandra-password',
                        help="Cassandra user password (default: %(default)s)",
                        default=None)
    parser.add_argument('--cassandra-use-ssl',
                        help="Cassandra use SSL flag (default: %(default)s)",
                        default=None)
    parser.add_argument('--cassandra-ca-certs',
                        help="Cassandra CA certs file path (default: %(default)s)",
                        default=None)
    parser.add_argument('--db-prefix',
                        help="Cassandra keyspace prefix "
                             "(default: %(default)s)",
                        default="")
    parser.add_argument('--cassandra-batch-size',
                        type=int,
                        help="Job queue size for cassandra batch "
                             "(default: %(default)s)",
                        default=LoadDataBase.BATCH_QUEUE_SIZE)
    parser.add_argument('--dont-populate-zookeeper', action='store_true',
                        help="Do not populate zookeeper database which is "
                             "very slow and may take time "
                             "(default: %(default)s)",
                        default=False)
    parser.add_argument('--zookeeper-servers',
                        help="Zookeeper server list (default: %(default)s)",
                        nargs='+',
                        default=['localhost:2181'])
    parser.add_argument('--rules-per-security-group',
                        type=int,
                        help="Rules ramdomly generated per created security "
                             "group (default: %(default)s)",
                        default=LoadDataBase.RULES_PER_SG)
    ksession.Session.register_cli_options(parser)
    kauth.register_argparse_arguments(parser, argv)

    params, _ = parser.parse_known_args(argv)

    try:
        keystone_auth = kauth.load_from_argparse_arguments(params)
        keystone_session = ksession.Session.load_from_cli_options(
            params, auth=keystone_auth)
        keystone_client = kclient.Client(session=keystone_session)
    except kexceptions.DiscoveryFailure:
        keystone_client = None

    param_dict = vars(params)
    param_dict['keystone_client'] = keystone_client
    for opt in ksession.Session.get_conf_options():
        try:
            param_dict.pop('os_%s' % opt.dest)
        except KeyError:
            param_dict.pop('%s' % opt.dest, None)
    for opt in\
            kauth.base.get_plugin_class(params.os_auth_plugin).get_options():
        param_dict.pop('os_%s' % opt.dest)
    param_dict = {k: v for k, v in param_dict.items()
                  if not k.startswith('os_')}

    database_loader = LoadDataBase(**param_dict)
    database_loader.sanitize_resources()
    database_loader.summarize_resources_to_create()
    database_loader.create_resources()


if __name__ == "__main__":
        main()
