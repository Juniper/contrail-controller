from collections import namedtuple
from collections import OrderedDict
import contextlib
import copy
from datetime import datetime
import sys
import time

import six


ColumnFamilyDef = namedtuple('ColumnFamilyDef', 'keyspace')

class FakeUtils(object):
    @staticmethod
    def convert_uuid_to_time(time_uuid_in_db):
        ts = time.mktime(time_uuid_in_db.timetuple())
        return ts


class NotFoundException(Exception):
    pass


class FakeConnection(object):
    def __init__(*args, **kwargs):
        pass

    @staticmethod
    def default_socket_factory(*args, **kwargs):
        pass


class FakeSystemManager(object):
    SIMPLE_STRATEGY = 'SimpleStrategy'

    class SystemManager(object):
        _keyspaces = {}

        def __init__(*args, **kwargs):
            pass

        def create_keyspace(self, name, *args, **kwargs):
            if name not in self._keyspaces:
                self._keyspaces[name] = {}

        def list_keyspaces(self):
            return list(self._keyspaces.keys())

        def get_keyspace_properties(self, ks_name):
            return {'strategy_options': {'replication_factor': '1'}}

        def get_keyspace_column_families(self, keyspace):
            return self._keyspaces.get(keyspace, {})

        def create_column_family(self, keyspace, name, *args, **kwargs):
            self._keyspaces[keyspace][name] = {}

        def drop_keyspace(self, ks_name):
            try:
                del self._keyspaces[ks_name]
            except KeyError:
                pass

        @classmethod
        @contextlib.contextmanager
        def patch_keyspace(cls, ks_name, ks_val=None):
            try:
                orig_ks_val = cls._keyspaces[ks_name]
                orig_existed = True
            except KeyError:
                orig_existed = False

            try:
                cls._keyspaces[ks_name] = ks_val
                yield
            finally:
                if orig_existed:
                    cls._keyspaces[ks_name] = orig_ks_val
                else:
                    del cls._keyspaces[ks_name]


class CassandraCFs(object):
    _all_cfs = {}

    @classmethod
    def add_cf(cls, keyspace, cf_name, cf):
        CassandraCFs._all_cfs[keyspace + '_' + cf_name] = cf

    @classmethod
    def get_cf(cls, keyspace, cf_name):
        return CassandraCFs._all_cfs[keyspace + '_' + cf_name]

    @classmethod
    def reset(cls, cf_list=None):
        if cf_list:
            for name in cf_list:
                if name in cls._all_cfs:
                    del cls._all_cfs[name]
            return
        cls._all_cfs = {}


class FakePool(object):
    class AllServersUnavailable(Exception):
        pass

    class MaximumRetryException(Exception):
        pass

    class ConnectionPool(object):
        def __init__(*args, **kwargs):
            self = args[0]
            if "keyspace" in kwargs:
                self.keyspace = kwargs['keyspace']
            else:
                self.keyspace = args[1]


class PatchContext(object):
    def __init__(self, cf):
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
                if row_key in cf._rows:
                    patched['row_existed'] = True
                    patched['orig_cols'] = copy.deepcopy(cf._rows[row_key])
                    if new_columns is None:
                        # simulates absence of key in cf
                        del cf._rows[row_key]
                    else:
                        cf._rows[row_key] = new_columns
                else:  # row didn't exist, create one
                    patched['row_existed'] = False
                    cf.insert(row_key, new_columns)
            elif patch_type == 'column':
                patched['type'] = 'column'
                row_key, col_name, col_val = patch_info
                patched['row_key'] = row_key
                patched['col_name'] = col_name
                if col_name in cf._rows[row_key]:
                    patched['col_existed'] = True
                    patched['orig_col_val'] = copy.deepcopy(
                        cf._rows[row_key][col_name])
                    if col_val is None:
                        # simulates absence of col
                        del cf._rows[row_key][col_name]
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
                    cf._rows[row_key] = patched['orig_cols']
                else:
                    del cf._rows[row_key]
            elif patch_type == 'column':
                col_name = patched['col_name']
                if patched['col_existed']:
                    cf._rows[row_key][col_name] = patched['orig_col_val']
                else:
                    del cf._rows[row_key][col_name]
            else:
                raise Exception(
                    "Unknown patch type %s in un-patching" % patch_type)


class FakeColumnFamily(object):
    # 2 initializations for same CF get same contents
    _all_cf_rows = {}

    def __init__(*args, **kwargs):
        self = args[0]
        self._pool = args[1]
        self._name = args[2]
        self._ks_cf_name = '%s_%s' % (self._pool.keyspace, self._name)
        self._cfdef = ColumnFamilyDef(self._pool.keyspace)
        try:
            self._rows = self._all_cf_rows[self._ks_cf_name]
        except KeyError:
            self._all_cf_rows[self._ks_cf_name] = OrderedDict({})
            self._rows = self._all_cf_rows[self._ks_cf_name]

        self.column_validators = {}
        CassandraCFs.add_cf(self._pool.keyspace, self._name, self)

    def get_range(self, *args, **kwargs):
        columns = kwargs.get('columns', None)
        column_start = kwargs.get('column_start', None)
        column_finish = kwargs.get('column_finish', None)
        column_count = kwargs.get('column_count', 0)
        include_timestamp = kwargs.get('include_timestamp', False)

        for key in self._rows:
            try:
                col_dict = self.get(key, columns, column_start, column_finish,
                                    column_count, include_timestamp)
                yield (key, col_dict)
            except NotFoundException:
                pass

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

    def get(self, key, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False, include_ttl=False):
        if not isinstance(key, six.string_types):
            raise TypeError("A str or unicode value was expected, but %s "
                            "was received instead (%s)"
                            % (key.__class__.__name__, str(key)))
        if isinstance(key, six.binary_type):
            key = key.decode('utf-8')
        if key not in self._rows:
            raise NotFoundException
        if columns:
            col_dict = {}
            for col_name in columns:
                try:
                    col_value = self._rows[key][col_name][0]
                except KeyError:
                    if len(columns) > 1:
                        continue
                    else:
                        raise NotFoundException
                if include_timestamp or include_ttl:
                    ret = (col_value,)
                    if include_timestamp:
                        col_tstamp = self._rows[key][col_name][1]
                        ret += (col_tstamp,)
                    if include_ttl:
                        col_ttl = self._rows[key][col_name][2]
                        ret += (col_ttl,)
                    col_dict[col_name] = ret
                else:
                    col_dict[col_name] = col_value
        else:
            col_dict = {}
            for col_name in list(self._rows[key].keys()):
                if not self._column_within_range(
                        col_name, column_start, column_finish):
                    continue

                col_value = self._rows[key][col_name][0]
                if include_timestamp or include_ttl:
                    ret = (col_value,)
                    if include_timestamp:
                        col_tstamp = self._rows[key][col_name][1]
                        ret += (col_tstamp,)
                    if include_ttl:
                        col_ttl = self._rows[key][col_name][2]
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
        if key not in self._rows:
            self._rows[key] = {}

        tstamp = (datetime.utcnow() - datetime(1970, 1, 1)).total_seconds()
        for col_name in list(col_dict.keys()):
            self._rows[key][col_name] = (col_dict[col_name], tstamp, ttl)

    def remove(self, key, columns=None):
        try:
            if columns:
                # for each entry in col_name delete each that element
                for col_name in columns:
                    del self._rows[key][col_name]
            elif columns is None:
                del self._rows[key]
        except KeyError:
            # pycassa remove ignores non-existing keys
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
        if key in self._rows:
            col_names = list(self._rows[key].keys())

        counter = 0
        for col_name in col_names:
            if self._column_within_range(
                    col_name, column_start, column_finish):
                counter += 1

        return counter

    def batch(self):
        return self

    def send(self):
        pass

    @contextlib.contextmanager
    def patch_cf(self, new_contents=None):
        orig_contents = self._all_cf_rows[self._ks_cf_name]
        try:
            self._all_cf_rows[self._ks_cf_name] = new_contents
            yield
        finally:
            self._all_cf_rows[self._ks_cf_name] = orig_contents

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


class FakeBatch(object):
    class Mutator(object):
        def send(self):
            pass


class FakeTtypes(object):
    class InvalidRequestException(Exception):
        pass

    class ConsistencyLevel(object):
        QUORUM = 42


def patch_imports(imports):
    for import_str, fake in imports:
        cur_module = None
        for mod_part in import_str.split('.'):
            if not cur_module:
                cur_module = mod_part
            else:
                cur_module += "." + mod_part
        sys.modules[cur_module] = fake


connection = FakeConnection
ConnectionPool = FakePool.ConnectionPool
ColumnFamily = FakeColumnFamily
util = FakeUtils
sys.modules["pycassa.batch"] = FakeBatch
sys.modules["pycassa.connection"] = FakeConnection
sys.modules["pycassa.system_manager"] = FakeSystemManager
sys.modules["pycassa.pool"] = FakePool
sys.modules["pycassa.cassandra"] = FakeTtypes
sys.modules["pycassa.cassandra.ttypes"] = FakeTtypes
sys.modules["pycassa.cassandra.ttypes.ConsistencyLevel"] =\
    FakeTtypes.ConsistencyLevel
