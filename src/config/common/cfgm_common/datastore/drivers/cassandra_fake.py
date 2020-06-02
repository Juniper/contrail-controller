#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import datetime
import itertools

from cfgm_common.datastore import api as datastore_api
from cfgm_common.datastore.drivers import cassandra_thrift
from cfgm_common.datastore.drivers import cassandra_cql
from cfgm_common.tests import cassandra_fake_impl


# This is to articulate the Thrift's driver against the fake
# Cassandra's server.
class CassandraDriverThrift(cassandra_thrift.CassandraDriverThrift):

    def __init__(self, server_list, **options):
        datastore_api.CassandraDriver.__init__(self, server_list, **options)

    def _Init_Cluster(self, *args, **kwargs):
        self.server = cassandra_fake_impl.CassandraFakeServer()
        for ks, cf_dict in itertools.chain(
                self.options.rw_keyspaces.items(),
                self.options.ro_keyspaces.items()):
            # Create keyspace
            keyspace = self.keyspace(ks)
            if self.options.reset_config:
                self.server.drop_keyspace(keyspace)
            ses = self.server.create_keyspace(keyspace)
            # Create CFs in keyspace
            for cf_name in cf_dict:
                self.create_session(keyspace, cf_name)

    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        return self._cf_dict[cf_name]

    def _Column_Families(self, keyspace, prefixed=False):
        if not prefixed:
            keyspace = self.keyspace(keyspace)
        return self.server.__keyspaces__[keyspace].__tables__.keys()

    def _Create_Session(self, keyspace, cf_name, **cf_args):
        self._cf_dict[cf_name] = (
            self.server.create_keyspace(keyspace).create_table(cf_name))

    def _Keyspace_Properties(self, keyspace):
        return {'strategy_options': {'replication_factor': '1'}}

    def _handle_exceptions(self, func, oper=None):
        def wrapper(*args, **kwargs):
            try:
                # Workaround added to statisfy test. Can be removed
                # when [0] merged.
                # [0] https://review.opencontrail.org/c/Juniper/contrail-controller/+/60779
                self.start_time = datetime.datetime.now()
                return func(*args, **kwargs)
            finally:
                if self.log_response_time:
                    self.end_time = datetime.datetime.now()
                    self.log_response_time(
                        self.end_time - self.start_time, oper)
        return wrapper


# This is to articulate the CQL's driver against the fake
# Cassandra's server.
class CassandraDriverCQL(cassandra_cql.CassandraDriverCQL):

    def __init__(self, server_list, **options):
        datastore_api.CassandraDriver.__init__(self, server_list, **options)

    def _Init_Cluster(self, *args, **kwargs):
        self.server = cassandra_fake_impl.CassandraFakeServer()
        for ks, cf_dict in itertools.chain(
                self.options.rw_keyspaces.items(),
                self.options.ro_keyspaces.items()):
            # Create keyspace
            keyspace = self.keyspace(ks)
            if self.options.reset_config:
                self.server.drop_keyspace(keyspace)
            ses = self.server.create_keyspace(keyspace)
            # Create CFs in keyspace
            for cf_name in cf_dict:
                self.create_session(keyspace, cf_name)

    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        return self._cf_dict[cf_name]

    def _Column_Families(self, keyspace, prefixed=False):
        if not prefixed:
            keyspace = self.keyspace(keyspace)
        return self.server.__keyspaces__[keyspace].__tables__.keys()

    def _Create_Session(self, keyspace, cf_name, **cf_args):
        self._cf_dict[cf_name] = (
            self.server.create_keyspace(keyspace).create_table(cf_name))

    def _Keyspace_Properties(self, keyspace):
        return {'strategy_options': {'replication_factor': '1'}}

    def _handle_exceptions(self, func, oper=None):
        def wrapper(*args, **kwargs):
            try:
                # Workaround added to statisfy test. Can be removed
                # when [0] merged.
                # [0] https://review.opencontrail.org/c/Juniper/contrail-controller/+/60779
                self.start_time = datetime.datetime.now()
                return func(*args, **kwargs)
            finally:
                if self.log_response_time:
                    self.end_time = datetime.datetime.now()
                    self.log_response_time(
                        self.end_time - self.start_time, oper)
        return wrapper
