import abc
import collections
import copy
import cProfile
import functools
import os
import six

from sandesh_common.vns import constants as vns_constants
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType


# Defines the global keyspaces and columns familly names/options.

# Name to ID mapping keyspace + tables
UUID_KEYSPACE_NAME = vns_constants.API_SERVER_KEYSPACE_NAME

# TODO describe layout
OBJ_UUID_CF_NAME = 'obj_uuid_table'

# TODO describe layout
OBJ_FQ_NAME_CF_NAME = 'obj_fq_name_table'

# key: object type, column ($type:$id, uuid)
# where type is entity object is being shared with. Project initially
OBJ_SHARED_CF_NAME = 'obj_shared_table'

UUID_KEYSPACE = {
    UUID_KEYSPACE_NAME: {
        OBJ_UUID_CF_NAME: {
            'cf_args': {
                'autopack_names': False,
                'autopack_values': False,
            },
        },
        OBJ_FQ_NAME_CF_NAME: {
            'cf_args': {
                'autopack_values': False,
            },
        },
        OBJ_SHARED_CF_NAME: {}
    }
}


# Defines API option type and defaults that will be passed to the
# drivers. The usage is to initialize a driver by listing only the
# necessary options.

OptionsDefault = {
    'db_prefix': '',
    'rw_keyspaces': {},
    'ro_keyspaces': {},
    'logger': None,
    # if no generate_url is specified, use a dummy function that
    # always returns an empty string
    'generate_url': lambda x, y: '',
    'reset_config': False,
    'credential': None,
    'walk': True,
    'obj_cache_entries': 0,
    'obj_cache_exclude_types': None,
    'debug_obj_cache_types': None,
    'log_response_time': None,
    'ssl_enabled': False,
    'ca_certs': None,
    'pool_size': 0,
}
OptionsType = collections.namedtuple(
    'Options', OptionsDefault.keys())


# Defines base class to trace drivers calls to Cassandra.  The results
# will be stored in files `profile.cassandra.<function>.trace`. They
# can be read using `pstats` or more evoluated tools like kcachegrind
# to find bottlenecks.

class Trace(object):
    def __init__(self):
        self._trace_track = {}
        self._trace_enabled = bool(int(
            # When defined and equal to 1, profiling the executions.
            os.getenv('CONTRAIL_PROFILE_CASSANDRA', 0)))

    @staticmethod
    def trace(f):
        @functools.wraps(f)
        def wrapped(self, *args, **kwargs):
            if self._trace_enabled:
                if f.__name__ not in self._trace_track:
                    self._trace_track[f.__name__] = cProfile.Profile()

                p = self._trace_track[f.__name__]
                p.enable()
                r = f(self, *args, **kwargs)
                p.disable()

                # TODO((sahid): We dump the information after each call. a bit
                # expensive, a better way could be found later.
                p.dump_stats("profile.cassandra.{}.trace".format(
                    f.__name__))
                return r
            else:
                return f(self, *args, **kwargs)
        return wrapped


# Defines API that drivers should implement.

@six.add_metaclass(abc.ABCMeta)
class API(Trace):

    def __init__(self, server_list, **options):
        super(API, self).__init__()

        self.options = copy.deepcopy(OptionsDefault)
        self.options.update(
            # This to filter inputs that are None, in that case we
            # prefer to use OptionDefault values.
            dict([(k, v) for k, v in options.items() if v is not None]))
        self.options = OptionsType(**self.options)

        self._server_list = server_list
        self._conn_state = ConnectionStatus.INIT

        # TODO(sahid): should be removed we sure of that is not used
        # by anything else.
        self.log_response_time = self.options.log_response_time

        # TODO(sahid): should be removed we sure of that is not used
        # by anything else.
        self._generate_url = self.options.generate_url

        # This dict handle the CFs session initialized to interact
        # with Cassandra.
        self._cf_dict = {}

        # TODO(sahid): The usage of ro_keyspaces/rw_keyspaces is not
        # clear and probably does not provide the isolation that we
        # want.
        if ((UUID_KEYSPACE_NAME not in self.options.ro_keyspaces) and
            (UUID_KEYSPACE_NAME not in self.options.rw_keyspaces)):
            # UUID_KEYSPACE_NAME represents the main keyspace used by
            # api-server. It is not expected that any other services
            # to write on it. If the driver is not initialized with
            # special requierment for it, we set as read-only mode.
            self.options.ro_keyspaces.update(UUID_KEYSPACE)

    @abc.abstractmethod
    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        pass

    @Trace.trace
    def get_cf_batch(self, cf_name, keyspace_name=None):
        """Get batch object bind to a column family used in insert/remove"""
        return self._Get_CF_Batch(cf_name=cf_name, keyspace_name=keyspace_name)

    @abc.abstractmethod
    def _Get_Range(self, cf_name, columns=None, column_count=100000):
        pass

    @Trace.trace
    def get_range(self, cf_name, columns=None, column_count=100000):
        """List all column family rows"""
        return self._Get_Range(
            cf_name=cf_name, columns=columns, column_count=column_count)

    @abc.abstractmethod
    def _Multiget(self, cf_name, keys, columns=None, start='', finish='',
                  timestamp=False, num_columns=None):
        pass

    @Trace.trace
    def multiget(self, cf_name, keys, columns=None, start='', finish='',
                 timestamp=False, num_columns=None):
        """List multiple rows on a column family"""
        return self._Multiget(
            cf_name=cf_name, keys=keys, columns=columns, start=start,
            finish=finish, timestamp=timestamp, num_columns=num_columns)

    @abc.abstractmethod
    def _Get(self, keyspace_name, cf_name, key, columns=None, start='',
             finish=''):
        pass

    @Trace.trace
    def get(self, cf_name, key, columns=None, start='', finish=''):
        """Fetch one row in a column family"""
        return self._Get(
            cf_name, key, columns=columns, start=start, finish=finish)

    @abc.abstractmethod
    def _XGet(self, cf_name, key, columns=None, start='', finish=''):
        pass

    @Trace.trace
    def xget(self, cf_name, key, columns=None, start='', finish=''):
        """Like get but creates a generator that pages over the columns automatically."""
        return self._XGet(
            cf_name=cf_name, key=key, columns=columns, start=start, finish=finish)

    @abc.abstractmethod
    def _Get_Count(self, cf_name, key, start='', finish='', keyspace_name=None):
        pass

    @Trace.trace
    def get_count(self, cf_name, key, start='', finish='', keyspace_name=None):
        """Count rows in a column family"""
        return self._Get_Count(
            cf_name=cf_name, key=key, start=start, finish=finish,
            keyspace_name=keyspace_name)

    @abc.abstractmethod
    def _Get_One_Col(self, cf_name, key, column):
        pass

    @Trace.trace
    def get_one_col(self, cf_name, key, column):
        """Fetch one column of a row in a column family"""
        return self._Get_One_Col(cf_name=cf_name, key=key, column=column)

    @abc.abstractmethod
    def _Insert(self, key, columns, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        pass

    @Trace.trace
    def insert(self, key, columns, keyspace_name=None, cf_name=None,
               batch=None, column_family=None):
        """Insert columns with value in a row in a column family"""
        self._Insert(key=key, columns=columns, keyspace_name=keyspace_name,
                     cf_name=cf_name, batch=batch, column_family=column_family)

    @abc.abstractmethod
    def _Remove(self, key, columns=None, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        pass

    @Trace.trace
    def remove(self, key, columns=None, keyspace_name=None, cf_name=None,
               batch=None, column_family=None):
        """Remove a specified row or a set of columns within the row"""
        self._Remove(key=key, columns=columns, keyspace_name=keyspace_name,
                     cf_name=cf_name, batch=batch, column_family=column_family)


# Defines common methods/functions that will be useful for the drivers.

class CassandraDriver(API):

    def keyspace(self, name):
        """Returns well formatted keyspace name with its prefix."""
        if self.options.db_prefix:
            return "{}_{}".format(self.options.db_prefix, name)
        return name

    def pool_size(self):
        """Returns size of pool based on options or deducts it from `_server_list`."""
        if self.options.pool_size == 0:
            return 2 * len(self._server_list)
        return self.options.pool_size

    def nodes(self):
        """Returns numbers of nodes in cluster."""
        # TODO(sahid): I copied that part from legacy. That part may
        # be wrong, cassandra has a principle of auto-discovery. We
        # could pass only one server's address to server_list,
        # cassandra will still automatically discovers its peers.
        return len(self._server_list)

    def report_status_init(self):
        """Informs to sandesh the `INIT` status of the cluster."""
        self._update_sandesh_status(ConnectionStatus.INIT)

    def report_status_up(self):
        """Informs to sandesh the `UP` status of the cluster."""
        self._update_sandesh_status(ConnectionStatus.UP)

    def report_status_down(self, reason=''):
        """Informs to sandesh the `DOWN` status of the cluster."""
        self._update_sandesh_status(ConnectionStatus.DOWN, msg=reason)

    def get_status(self):
        """Returns current sendesh status of the cluster."""
        return self._conn_state

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnType.DATABASE,
                               name='Cassandra', status=status, message=msg,
                               server_addrs=self._server_list)
        # Keeps trace of the current status.
        self._conn_state = status
