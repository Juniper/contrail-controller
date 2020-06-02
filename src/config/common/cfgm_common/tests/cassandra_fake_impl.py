#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import collections
from collections import OrderedDict
import contextlib
import copy
from datetime import datetime
import sys
import time

import six


try:
    # Even if we don't want import pycassa anymore. Only
    # CassandraDriverThrift should doing that. We have to because some
    # part of the code like db_manage.py is still refering some
    # pycassa objects.
    import pycassa.NotFoundException

    BaseExceptionClass = pycassa.NotFoundException
except ImportError:
    BaseExceptionClass = Exception


class NotFoundException(BaseExceptionClass):
    pass


def reset():
    CassandraFakeServer.__keyspaces__ = {}


# This is implementating a *kind* of Cassandra server to run tests, in
# future we should remove that and is real instamce of Cassandra.
class CassandraFakeServer(object):
    # The servers share the keyspaces
    __keyspaces__ = {}

    @classmethod
    def create_keyspace(cls, name):
        if name not in cls.__keyspaces__:
            cls.__keyspaces__[name] = _CassandraFakeServerKeyspace(name)
        return cls.__keyspaces__[name]

    @classmethod
    def drop_keyspace(cls, name):
        if name in cls.__keyspaces__:
            del cls.__keyspaces__[name]

    @classmethod
    @contextlib.contextmanager
    def patch_keyspace(cls, ks_name, ks_val=None):
        try:
            orig_ks_val = cls.__keyspaces__[ks_name]
            orig_existed = True
        except KeyError:
            orig_existed = False

        try:
            cls.create_keyspace(ks_name)
            cls.__keyspaces__[ks_name].__tables__ = ks_val
            yield
        finally:
            if orig_existed:
                cls.__keyspaces__[ks_name].__tables__ = orig_ks_val
            else:
                cls.drop_keyspace(ks_name)


class _CassandraFakeServerKeyspace(object):
    def __init__(self, name):
        self.name = name
        self.__tables__ = {}

    def create_table(self, name):
        if name not in self.__tables__:
            self.__tables__[name] = _CassandraFakeServerTable(
                self.name, name)
        return self.__tables__[name]

    def drop_table(self, name):
        if name in self.__tables__:
            del self.__tables__[name]


# This is adding CQL support to the server
class _TableCQLSupport(object):
    QueryType = collections.namedtuple('Query', (
        'method', 'conditions', 'limit'))

    def __init__(self, name):
        # CQL driver related
        self.cf_name = name

    def prepare(self, cql):
        query = self._parse_cql(cql)

        class Prepare(object):

            def __init__(self, query):
                self.query = query

            def bind(self, args):
                self.args = args
                for idx, condition in enumerate(self.query.conditions):
                    # Apply bindings
                    condition['value'] = args[idx]
                return self

        return Prepare(query)

    def add_insert(self, key, cql, args):
        return self.execute(self.prepare(cql).bind(args))

    def add_remove(self, key, cql, args):
        return self.execute(self.prepare(cql).bind(args))

    def execute_async(self, prepare, args=None):
        exc = self.execute

        class Future(object):
            def result(self):
                return exc(prepare, args)
        return Future()

    def execute(self, prepare, args=None):
        if args is not None:
            return self.execute(self.prepare(prepare).bind(args))
        query = prepare.query
        if query.method == "insert":
            return self.insert(
                key=prepare.args[0],
                col_dict={prepare.args[1]: prepare.args[2]})
        elif query.method == "remove":
            columns = None
            if len(prepare.args) > 1:
                columns = [prepare.args[1]]
            return self.remove(key=prepare.args[0], columns=columns)

        args = {}
        if query.conditions:
            for condition in query.conditions:
                if condition["key"] == "key":
                    args["key"] = condition["value"]
                elif condition["operator"] == ">=":
                    args["column_start"] = condition["value"]
                elif condition["operator"] == "<=":
                    args["column_finish"] = condition["value"]
                if query.limit:
                    args["column_count"] = query.limit
            if query.method == "get":
                args["include_timestamp"] = True
                try:
                    return [(c, v[0], v[1])
                            for c, v in self.get(**args).items()]
                except NotFoundException:
                    return []
            elif query.method == "get_count":

                class Result(object):

                    def __init__(self, result):
                        self.result = result

                    def one(self):
                        return self.result

                return Result([self.get_count(**args)])
        else:
            # Not an insert or a remove, and without condition, should
            # be a get_range.
            def generator():
                args["include_timestamp"] = True
                for key, col_dict in self.get_range(**args):
                    for column, value in col_dict.items():
                        yield key, column, value[0], value[1]
            return generator()

    def _parse_cql(self, cql):
        limit, conditions = None, []

        def cql_select(it):
            while it:
                curr = next(it).strip()
                if curr.upper().startswith("FROM"):
                    return "get"
                if curr.upper().startswith("COUNT"):
                    return "get_count"

        def cql_from(it):
            return next(it).strip('"')

        def cql_condition(it):
            return {'key': next(it),
                    'operator': next(it),
                    'value': next(it)}

        def cql_limit(it):
            return int(next(it))

        it = iter(cql.split())
        while it:
            try:
                curr = next(it).strip()
            except StopIteration:
                break

            if "SELECT" in curr.upper():
                method = cql_select(it)
            if "INSERT" in curr.upper():
                method = "insert"
            if "DELETE" in curr.upper():
                method = "remove"
            if "WHERE" in curr.upper() or "AND" in curr.upper():
                conditions.append(cql_condition(it))
            if "LIMIT" in curr.upper():
                limit = cql_from(it)

        return self.QueryType(method, conditions, limit)


class _CassandraFakeServerTable(_TableCQLSupport):

    ColumnFamilyDefType = collections.namedtuple(
        'ColumnFamilyDef', 'keyspace')

    def __init__(self, keyspace, name):
        super(_CassandraFakeServerTable, self).__init__(name)

        self.keyspace = keyspace
        self.name = name

        self.__rows__ = {}

        # Thrift related
        self._cfdef = self.ColumnFamilyDefType(keyspace)

    def send(self):
        pass

    def get(self, key, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False, include_ttl=False):
        if not isinstance(key, six.string_types):
            raise TypeError("A str or unicode value was expected, but %s "
                            "was received instead (%s)"
                            % (key.__class__.__name__, str(key)))
        if isinstance(key, six.binary_type):
            key = key.decode('utf-8')
        if key not in self.__rows__:
            raise NotFoundException
        if columns:
            col_dict = {}
            for col_name in columns:
                try:
                    col_value = self.__rows__[key][col_name][0]
                except KeyError:
                    if len(columns) > 1:
                        continue
                    else:
                        raise NotFoundException
                if include_timestamp or include_ttl:
                    ret = (col_value,)
                    if include_timestamp:
                        col_tstamp = self.__rows__[key][col_name][1]
                        ret += (col_tstamp,)
                    if include_ttl:
                        col_ttl = self.__rows__[key][col_name][2]
                        ret += (col_ttl,)
                    col_dict[col_name] = ret
                else:
                    col_dict[col_name] = col_value
        else:
            col_dict = {}
            for col_name in list(self.__rows__[key].keys()):
                if not self._column_within_range(
                        col_name, column_start, column_finish):
                    continue

                col_value = self.__rows__[key][col_name][0]
                if include_timestamp or include_ttl:
                    ret = (col_value,)
                    if include_timestamp:
                        col_tstamp = self.__rows__[key][col_name][1]
                        ret += (col_tstamp,)
                    if include_ttl:
                        col_ttl = self.__rows__[key][col_name][2]
                        ret += (col_ttl,)
                    col_dict[col_name] = ret
                else:
                    col_dict[col_name] = col_value

        if len(col_dict) == 0:
            raise NotFoundException

        sorted_col_dict = OrderedDict(
            (k, col_dict[k]) for k in sorted(col_dict))
        return sorted_col_dict

    def multiget(
        self, keys, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False):
        result = {}
        for key in keys:
            try:
                col_dict = self.get(key, columns, column_start, column_finish,
                                    column_count, include_timestamp)
                result[key] = col_dict
            except NotFoundException:
                pass

        return result

    def insert(self, key, col_dict, ttl=None):
        if key not in self.__rows__:
            self.__rows__[key] = {}

        tstamp_sec = (datetime.utcnow() - datetime(1970, 1, 1)).total_seconds()
        tstamp = int(tstamp_sec * 1000)  # uint milliseconds
        for col_name in list(col_dict.keys()):
            self.__rows__[key][col_name] = (col_dict[col_name], tstamp, ttl)

    def remove(self, key, columns=None):
        try:
            if columns:
                # for each entry in col_name delete each that element
                for col_name in columns:
                    del self.__rows__[key][col_name]
            elif columns is None:
                del self.__rows__[key]
        except KeyError:
            # pycassa remove ignores non-existing keys
            pass

    def get_range(self, *args, **kwargs):
        columns = kwargs.get('columns', None)
        column_start = kwargs.get('column_start', None)
        column_finish = kwargs.get('column_finish', None)
        column_count = kwargs.get('column_count', 0)
        include_timestamp = kwargs.get('include_timestamp', False)

        for key in copy.copy(self.__rows__):
            try:
                col_dict = self.get(key, columns, column_start, column_finish,
                                    column_count, include_timestamp)
                yield (key, col_dict)
            except NotFoundException:
                pass

    def xget(self, key, column_start=None, column_finish=None,
             column_count=0, include_timestamp=False, include_ttl=False):
        try:
            col_dict = self.get(key,
                                column_start=column_start,
                                column_finish=column_finish,
                                column_count=column_count,
                                include_timestamp=include_timestamp,
                                include_ttl=include_ttl)
        except NotFoundException:
            col_dict = {}

        for k, v in list(col_dict.items()):
            yield (k, v)

    def get_count(self, key, column_start=None, column_finish=None):
        col_names = []
        if key in self.__rows__:
            col_names = list(self.__rows__[key].keys())

        counter = 0
        for col_name in col_names:
            if self._column_within_range(
                    col_name, column_start, column_finish):
                counter += 1

        return counter

    def _column_within_range(self, column_name, column_start, column_finish):
        if type(column_start) is tuple:
            for i in range(len(column_start), len(column_name)):
                column_start = column_start + (column_name[i],)
        if type(column_finish) is tuple:
            for i in range(len(column_finish), len(column_name)):
                column_finish = column_finish + (column_name[i],)

        if column_start and column_name < column_start:
            return False
        if column_finish and column_name > column_finish:
            return False

        return True

    @contextlib.contextmanager
    def patch_cf(self, new_contents=None):
        orig_contents = CassandraFakeServer.__keyspaces__[self.keyspace].\
            __tables__[self.name].__rows__
        try:
            CassandraFakeServer.__keyspaces__[self.keyspace].\
                __tables__[self.name].__rows__ = new_contents
            yield
        finally:
            CassandraFakeServer.__keyspaces__[self.keyspace].\
                __tables__[self.name].__rows__ = orig_contents

    @contextlib.contextmanager
    def patch_row(self, key, new_columns=None):
        ctx = PatchContext(self)
        try:
            ctx.patch([('row', (key, new_columns))])
            yield
        finally:
            ctx.unpatch()

    @contextlib.contextmanager
    def patch_column(self, key, col_name, col_val=None):
        ctx = PatchContext(self)
        try:
            ctx.patch([('column', (key, col_name, col_val))])
            yield
        finally:
            ctx.unpatch()

    @contextlib.contextmanager
    def patches(self, patch_list):
        ctx = PatchContext(self)
        try:
            ctx.patch(patch_list)
            yield
        finally:
            ctx.unpatch()


class PatchContext(object):

    def __init__(self, cf):
        """Patch couple of rows in datastore."""
        self.cf = cf
        self.patches = []  # stack of patches

    def patch(self, patch_list):
        cf = self.cf
        for patch_type, patch_info in patch_list:
            patched = {}
            if patch_type == 'row':
                patched['type'] = 'row'
                row_key, new_columns = patch_info
                patched['row_key'] = row_key
                if row_key in cf.__rows__:
                    patched['row_existed'] = True
                    patched['orig_cols'] = copy.deepcopy(cf.__rows__[row_key])
                    if new_columns is None:
                        # simulates absence of key in cf
                        del cf.__rows__[row_key]
                    else:
                        cf.__rows__[row_key] = new_columns
                else:  # row didn't exist, create one
                    patched['row_existed'] = False
                    cf.insert(row_key, new_columns)
            elif patch_type == 'column':
                patched['type'] = 'column'
                row_key, col_name, col_val = patch_info
                patched['row_key'] = row_key
                patched['col_name'] = col_name
                if col_name in cf.__rows__[row_key]:
                    patched['col_existed'] = True
                    patched['orig_col_val'] = copy.deepcopy(
                        cf.__rows__[row_key][col_name])
                    if col_val is None:
                        # simulates absence of col
                        del cf.__rows__[row_key][col_name]
                    else:
                        cf.insert(row_key, {col_name: col_val})
                else:  # column didn't exist, create one
                    patched['col_existed'] = False
                    cf.insert(row_key, {col_name: col_val})
            else:
                raise Exception(
                    "Unknown patch type %s in patching" % patch_type)

            self.patches.append(patched)

    def unpatch(self):
        cf = self.cf
        for patched in reversed(self.patches):
            patch_type = patched['type']
            row_key = patched['row_key']
            if patch_type == 'row':
                if patched['row_existed']:
                    cf.__rows__[row_key] = patched['orig_cols']
                else:
                    del cf.__rows__[row_key]
            elif patch_type == 'column':
                col_name = patched['col_name']
                if patched['col_existed']:
                    cf.__rows__[row_key][col_name] = patched['orig_col_val']
                else:
                    del cf.__rows__[row_key][col_name]
            else:
                raise Exception(
                    "Unknown patch type %s in un-patching" % patch_type)


#
# This is to support modules that are still using pycassa (db_manage)
#

class FakePool(object):

    class AllServersUnavailable(Exception):  # noqa: D106
        pass

    class MaximumRetryException(Exception):  # noqa: D106
        pass

    class ConnectionPool(object):  # noqa: D106

        def __init__(*args, **kwargs):
            self = args[0]
            if "keyspace" in kwargs:
                self.keyspace = kwargs['keyspace']
            else:
                self.keyspace = args[1]


class FakeUtils(object):

    @staticmethod
    def convert_uuid_to_time(time_uuid_in_db):
        ts = time.mktime(time_uuid_in_db.timetuple())
        return ts


class FakeConnection(object):

    def __init__(*args, **kwargs):
        pass

    @staticmethod
    def default_socket_factory(*args, **kwargs):
        pass


class FakeSystemManager(CassandraFakeServer):

    SIMPLE_STRATEGY = 'SimpleStrategy'

    class SystemManager(object):  # noqa: D106

        def __init__(*args, **kwargs):
            pass

        def list_keyspaces(self):
            return list(CassandraFakeServer.__keyspaces__.keys())

        def get_keyspace_properties(self, ks_name):
            return {'strategy_options': {'replication_factor': '1'}}

        def get_keyspace_column_families(self, keyspace):
            return CassandraFakeServer.__keyspaces__.get(
                keyspace, {}).__tables__.keys()

        def create_column_family(self, keyspace, name, *args, **kwargs):
            CassandraFakeServer.__keyspaces__[keyspace].create_table(name)


class FakeColumnFamily(_CassandraFakeServerTable):

    def __init__(self, pool, name, *args, **kwargs):
        self._pool = pool
        self._name = name
        super(FakeColumnFamily, self).__init__(self._pool.keyspace, self._name)

        # We proxy pycassa and CassandraFakeServer
        ks = CassandraFakeServer.create_keyspace(self._pool.keyspace)
        self.__rows__ = ks.create_table(self._name).__rows__


class FakeBatch(object):

    class Mutator(object):  # noqa: D106
        def send(self):
            pass


class FakeTtypes(object):

    class InvalidRequestException(Exception):  # noqa: D106
        pass

    class ConsistencyLevel(object):  # noqa: D106
        QUORUM = 42


class FakePycassa(object):
    connection = FakeConnection
    ConnectionPool = FakePool.ConnectionPool
    ColumnFamily = FakeColumnFamily
    util = FakeUtils
    NotFoundException = NotFoundException


sys.modules["pycassa"] = FakePycassa
sys.modules["pycassa.batch"] = FakeBatch
sys.modules["pycassa.connection"] = FakeConnection
sys.modules["pycassa.system_manager"] = FakeSystemManager
sys.modules["pycassa.pool"] = FakePool
sys.modules["pycassa.cassandra"] = FakeTtypes
sys.modules["pycassa.cassandra.ttypes"] = FakeTtypes
sys.modules["pycassa.cassandra.ttypes.ConsistencyLevel"] =\
    FakeTtypes.ConsistencyLevel


class FakeTSSLSocket(object):
    class TSSLSocket(object):  # noqa: D106
        pass


class FakeThrift(object):
    transport = FakeTSSLSocket


sys.modules["thrift"] = FakeThrift
sys.modules["thrift.transport"] = FakeTSSLSocket
