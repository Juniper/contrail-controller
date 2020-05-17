#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

from datetime import datetime
import itertools

from cfgm_common.exceptions import NoIdError, VncError
from cfgm_common.datastore import api as datastore_api
from cfgm_common.tests import cassandra_fake_impl



# This is implementing CassandraFake'server to execute tests against
# CassandraFakeServer. Because of this we do not excerice our drivers
# against real instance of Cassandra, we should in future upgrade CI
# and tf-dev-env so we can execute the tests with real server.
class CassandraDriverFake(datastore_api.CassandraDriver):

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
                self._cf_dict[cf_name] = ses.create_table(cf_name)

    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        # TODO(sahid): We should implement batch so we can ensure that
        # we add in batch queries with same partition keys
        class Batch(object):
            def __init__(self, cf_name):
                self.cf_name = cf_name
            def send(self):
                return
            def add(self):
                return
        return Batch(cf_name)

    def _Get_Range(self, cf_name, columns=None, column_count=100000, include_timestamp=False):
        return self.get_cf(cf_name).get_range(
            columns=columns,
            column_count=column_count,
            include_timestamp=include_timestamp)

    def _Multiget(self, cf_name, keys, columns=None, start='', finish='',
                  timestamp=False, num_columns=None):
        results = self.get_cf(cf_name).multiget(
            keys=keys,
            columns=columns,
            column_start=start,
            column_finish=finish,
            include_timestamp=timestamp)
        return results

    def _Get(self, cf_name, key, columns=None, start='', finish=''):
        try:
            return self.get_cf(cf_name).get(
                key=key,
                columns=columns,
                column_start=start,
                column_finish=finish)
        except cassandra_fake_impl.NotFoundException:
            return None

    def _XGet(self, cf_name, key, columns=None, start='', finish=''):
        return self.get_cf(cf_name).xget(
            key=key,
            column_start=start,
            column_finish=finish)

    def _Get_Count(self, cf_name, key, start='', finish='', keyspace_name=None):
        return self.get_cf(cf_name).get_count(
            key=key, column_start=start, column_finish=finish)

    def _Get_One_Col(self, cf_name, key, column):
        col = self.multiget(cf_name, [key], columns=[column])
        if key not in col:
            raise NoIdError(key)
        elif len(col[key]) > 1:
            raise VncError('Multi match %s for %s' % (column, key))
        return col[key][column]

    def _Insert(self, key, columns, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        if batch:
            cf_name = batch.cf_name
        return self.get_cf(cf_name).insert(key=key, col_dict=columns)

    def _Remove(self, key, columns=None, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        if batch:
            cf_name = batch.cf_name
        return self.get_cf(cf_name).remove(key=key, columns=columns)

    def _Column_Families(self, keyspace, prefixed=False):
        pass

    def _Create_Session(self, keyspace, cf_name, **cf_args):
        pass

    def _handle_exceptions(self, func, oper=None):
        def wrapper(*args, **kwargs):
            return func(*args, **kwargs)
        return wrapper
