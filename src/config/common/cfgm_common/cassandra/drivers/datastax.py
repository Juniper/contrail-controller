#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import collections
import datetime
import gevent
import importlib
import itertools
import six
import ssl
import sys
import time

try:
    cassandra = importlib.import_module('cassandra')
    cassandra.cluster = importlib.import_module('cassandra.cluster')
    cassandra.query = importlib.import_module('cassandra.query')
    cassandra.cqltypes = importlib.import_module('cassandra.cqltypes')
    cassandra.protocol = importlib.import_module('cassandra.protocol')
    cassandra.cqlengine = importlib.import_module('cassandra.cqlengine')
    cassandra.cqlengine.models = importlib.import_module('cassandra.cqlengine.models')
    cassandra.cqlengine.columns = importlib.import_module('cassandra.cqlengine.columns')
    cassandra.cqlengine.management = importlib.import_module('cassandra.cqlengine.management')
except ImportError:
    cassandra = None

from cfgm_common.cassandra import api as cassa_api
from cfgm_common.exceptions import NoIdError, DatabaseUnavailableError, VncError
from cfgm_common import jsonutils as json
from cfgm_common import utils
from sandesh_common.vns import constants as vns_constants
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


# Seconds after data is marked with a tombstone (deletion marker)
# before it is eligible for garbage-collection. Default value: 864000
# (10 days). The default value allows time for the database to
# maximize consistency prior to deletion.
DEFAULT_GC_GRACE_SEC = vns_constants.CASSANDRA_DEFAULT_GC_GRACE_SECONDS

# Setting that defines a successful write or read by the number of
# cluster replicas that acknowledge the write or respond to the read
# request, respectively. e.g: QUORUM # ceil(RF/2) replicas must
# respond to consider the operation a success
DEFAULT_CONSISTENCY_LEVEL = cassandra.ConsistencyLevel.QUORUM

# TODO(sahid): Should find the minimal supported versions that we
# support and ship with Cassandra server.
CQL_VERSION = None
# We don't want to use any datastax features added in the protocol
# that to avoid been dependant of that connector.
PROTOCOL_VERSION = 2

# SSL
if sys.version_info >= (3, 6):
    # Python3.6 Introduces PROTOCOL_TLS which is using the best
    # version supported.
    SSL_VERSION = ssl.PROTOCOL_TLS
else:
    SSL_VERSION = ssl.PROTOCOL_TLSv1_2

# Set limit of the columns returned.
MAX_COLUMNS = 10000000

# Profiles related features
EXEC_PROFILE_DEFAULT = cassandra.cluster.EXEC_PROFILE_DEFAULT
EXEC_PROFILE_COUNT   = "EXEC_PROFILE_COUNT"
EXEC_PROFILE_SELECT  = "EXEC_PROFILE_SELECT"
EXEC_PROFILES        = {
    # Default profile, used by almost all the kind of queries
    EXEC_PROFILE_DEFAULT: cassandra.cluster.ExecutionProfile(
        consistency_level=DEFAULT_CONSISTENCY_LEVEL,
        row_factory=cassandra.query.dict_factory),
    # Count profile, used by _Get_Count methos
    EXEC_PROFILE_COUNT: cassandra.cluster.ExecutionProfile(
        # TODO(sahid): Do we really want QUORUM when counting?
        consistency_level=DEFAULT_CONSISTENCY_LEVEL,
        row_factory=lambda c, r: r),    
    # Select profile, used by _Get* methods
    EXEC_PROFILE_SELECT: cassandra.cluster.ExecutionProfile(
        consistency_level=DEFAULT_CONSISTENCY_LEVEL,
        row_factory=lambda c, r: collections.OrderedDict(r)),
}


# Decodes JSON string in Python Object.
JsonToObject = lambda x: json.loads(x)

# Imported from pycassa
GmTimestamp = lambda: int(time.time() * 1e6)


# This is encapsulating the ResultSet iterator that to provide feature
# to filter the columns, decode JSON or add timestamp.
class Iter(six.Iterator):
    def __init__(self, result, columns,
                 include_timestamp=False,
                 decode_json=True,
                 limit=MAX_COLUMNS):
        self.result = iter(result)
        self.columns = columns
        self.include_timestamp = include_timestamp
        self.decode_json = decode_json
        self.limit = min(MAX_COLUMNS, limit)

    def decode(self, v):
        if self.decode_json:
            return JsonToObject(v)
        return v

    def timestamp(self, v):
        if self.include_timestamp:
            return (v, GmTimestamp())
        return v

    def get_next_items(self):
        return self.result.__next__().items()

    def __iter__(self):
        return self

    def __next__(self):
        row = self.get_next_items()
        fcn = lambda x: self.timestamp(self.decode(x))
        if self.columns:
            row = collections.OrderedDict(
                [(k, fcn(v)) for k, v in row[:self.limit] if k in self.columns])
        else:
            row = collections.OrderedDict(
                [(k, fcn(v)) for k, v in row[:self.limit]])
        return row

    def one(self):
        try:
            return next(self)
        except StopIteration:
            return {}


# This is implementing our Cassandra API for the driver provided by
# DataStax, we try to stay simple, compatible with legacy, and finally
# compatible with Python2 as-well.  We have made the choice to avoid
# using the Object Mapping proposed by DataStax's driver to allow more
# flexibilities.  In a perfect world, any change here should be
# acompagned by an unittest to avoid regression.
class DataStax(cassa_api.CassandraDriver):
    def __init__(self, server_list, **options):
        global cassandra
        if cassandra is None:
            raise ImportError("the global 'cassandra' can't "
                              "be null at this step. Please verify "
                              "dependencies.")
        super(DataStax, self).__init__(server_list, **options)

    def _Init_Cluster(self):
        self.report_status_init()

        auth_provider = None
        if self.options.credential:
            auth_provider = cassandra.auth.PlainTextAuthProvider(
                username=self.options.credential.get('username'),
                password=self.options.credential.get('password'))
        ssl_options, ssl_context = None, None
        if self.options.ssl_enabled:
            ssl_context = ssl.SSLContext(SSL_VERSION)
            ssl_context.load_verify_locations(self.options.ca_certs)
            ssl_context.verify_mode = ssl.CERT_REQUIRED
            ssl_context.check_hostname = False
            ssl_options = {}

        # Connects client to the cassandra servers
        self._cluster = cassandra.cluster.Cluster(
            self._server_list,
            ssl_options=ssl_options,
            ssl_context=ssl_context,
            auth_provider=auth_provider,
            execution_profiles=EXEC_PROFILES,
            protocol_version=PROTOCOL_VERSION,
            cql_version=CQL_VERSION)

        # Initializes RW keyspaces
        for ks, cf_dict in self.options.rw_keyspaces.items():
            keyspace = self.keyspace(ks)
            if self.options.reset_config:
                self.safe_drop_keyspace(keyspace)
            self.safe_create_keyspace(keyspace)

        # Ensures RO keyspaces are initialized
        while not self.are_keyspaces_ready(self.options.ro_keyspaces):
            # Let's a chance to an other greenthread to be scheduled.
            gevent.sleep(1)

        # The CFs are flatten in a dict with the keyspaces' session
        # related.
        for ks, cf_dict in itertools.chain(
                self.options.rw_keyspaces.items(),
                self.options.ro_keyspaces.items()):
            for cf_name in cf_dict:
                self._cf_dict[cf_name] = self._cluster.connect(
                    self.keyspace(ks))

        # Now we create the tables/CFs if not already alive.
        for cf_name in self._cf_dict:
            self.safe_create_table(cf_name)

        self.report_status_up()

    def are_keyspaces_ready(self, keyspaces):
        """From a list of keyspaces, return False if one not yet available"""
        try:
            for ks, _ in keyspaces.items():
                self._cluster.connect(self.keyspace(ks))
        except cassandra.cluster.NoHostAvailable:
            return False
        return True

    def get_default_session(self):
        """Returns the default session, not connected to any keyspace"""
        # It is a singleton, we don't have to worry whether the
        # session has already been created.
        return self._cluster.connect()

    def safe_create_table(self, cf_name):
        """Creates table c.f ColumnFamilly if does not already exist"""
        ses = self.get_cf(cf_name)
        # We don't use IF EXISTS to print debug.
        cql = """
          CREATE TABLE "{}" (
            key blob,
            column1 blob,
            value text,
            PRIMARY KEY (key, column1)
          ) WITH CLUSTERING ORDER BY (column1 ASC)
            AND gc_grace_seconds = {}
          """.format(cf_name,
                     # TODO(sahid): Needs to investigate whether we
                     # need more options.
                     DEFAULT_GC_GRACE_SEC)
        try:
            ses.execute(cql)
            msg = "table '{}', created"
        except cassandra.protocol.AlreadyExists:
            msg = "table '{}', already created"
        self.options.logger(
            msg.format(cf_name), level=SandeshLevel.SYS_NOTICE)

    def safe_drop_keyspace(self, keyspace):
        """Drops keyspace if exists"""
        ses = self.get_default_session()
        # We don't use IF EXISTS to print debug.
        cql = """
          DROP KEYSPACE "{}"
        """.format(keyspace)
        try:
            ses.execute(cql)
            msg = "keyspace '{}', dropped"
        except cassandra.protocol.ConfigurationException:
            msg = "keyspace '{}', already dropped or does not exist"
        self.options.logger(
            msg.format(keyspace), level=SandeshLevel.SYS_NOTICE)

    def safe_create_keyspace(self, keyspace):
        """Creates keyspace if does not already exist"""
        ses = self.get_default_session()
        # We don't use IF EXISTS to print debug.
        cql = """
          CREATE KEYSPACE "{}"
          WITH replication = {{
            'class': 'SimpleStrategy',
            'replication_factor': '{}'
          }}""".format(keyspace, self.nodes())
        try:
            ses.execute(cql)
            msg = "keyspace '{}', created"
        except cassandra.protocol.AlreadyExists:
            msg = "keyspace '{}', already created"
        self.options.logger(
            msg.format(keyspace), level=SandeshLevel.SYS_NOTICE)

    def _cql_select(self, cf_name, key=None, start='', finish='', limit=None,
                    columns=None, include_timestamp=False, decode_json=True):
        ses = self.get_cf(cf_name)
        arg, cql = [], """
          SELECT blobAsText(column1), value FROM "{}"
        """.format(cf_name)
        if key:
            cql += "WHERE key = textAsBlob(?) "
            arg.append(six.text_type(key))
        if start:
            cql += "AND column1 > textAsBlob(?) "
            arg.append(six.text_type(start))
        if finish:
            cql += "AND column1 < textAsBlob(?) "
            arg.append(six.text_type(finish))
        if limit:
            cql += "LIMIT ? "
            arg.append(limit)
        pre = ses.prepare(cql)
        return Iter(ses.execute(pre.bind(arg), execution_profile=EXEC_PROFILE_SELECT),
                    # Filtering the columns using cassandra adds
                    # performance degradation it is more performance
                    # to let Python processes doing that job locally.
                    columns=columns,
                    include_timestamp=include_timestamp,
                    decode_json=decode_json)

    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        ses = self.get_cf(cf_name)
        bch = cassandra.query.BatchStatement(
            session=ses, consistency_level=DEFAULT_CONSISTENCY_LEVEL)
        # This hack is to make the behavior of `batch` from datastax
        # connector working with legacy which was thrift's connector
        # based.
        bch.cf_name = cf_name
        bch.send = lambda: ses.execute(bch).one()
        return bch

    def _Multiget(self, cf_name, keys, columns=None, start='', finish='',
                  timestamp=False, num_columns=None):
        try:
            num_columns = max(int(num_columns), num_columns)
        except:
            num_columns = MAX_COLUMNS

        res = {}
        # TODO(sahid): An importante optimisation is to execute the
        # requests in parallel. see: future/asyncio.
        for key in keys:
            row = self._cql_select(
                cf_name, key=key, start=start, finish=finish,
                columns=columns, include_timestamp=timestamp,
                limit=num_columns).one()
            if row:
                res[key] = row
        return res

    def _XGet(self, cf_name, key, columns=None, start='', finish=''):
        try:
            return next(self._cql_select(
                cf_name=cf_name,
                key=key,
                start=start,
                finish=finish,
                columns=columns,
                # TODO(sahid): In legacy, the implementation of `xget`
                # does not use `multiget` so does not decode the JSON
                # string in Python Object. We should try to be
                # coherent.
                decode_json=False
            )).items()
        except StopIteration:
            # Looks like the legacy is expecting an empty iterator.
            return iter({})

    def _Get(self, cf_name, key, columns=None, start='', finish=''):
        return self._cql_select(
            cf_name=cf_name,
            key=key,
            start=start,
            finish=finish,
            columns=columns).one()

    def _Get_One_Col(self, cf_name, key, column):
        itr = self._cql_select(cf_name=cf_name, key=key, columns=[column])
        res = itr.one()
        if not res:
            # Looks like legacy is expecting an exception.
            raise NoIdError(key)
        # This function also wants to ensure the result is uniq.
        try:
            next(itr)
        except StopIteration:
            return res[column]
        else:
            raise VncError("multi match {} for {}".format(column, key))

    def _Get_Range(self, cf_name, columns=None, column_count=100000):
        return self._cql_select(
            cf_name=cf_name, limit=column_count, columns=columns)

    def _Get_Count(self, cf_name, key, start='', finish='', keyspace_name=None):
        ses = self.get_cf(cf_name)
        arg, cql = [], """
          SELECT COUNT(*) FROM "{}"
          WHERE key = textAsBlob(?)
        """.format(cf_name)
        arg = [six.text_type(key)]
        if start:
            cql += "AND column1 > textAsBlob(?) "
            arg.append(six.text_type(start))
        if finish:
            cql += "AND column1 < textAsBlob(?) "
            arg.append(six.text_type(finish))
        pre = ses.prepare(cql)
        return ses.execute(
            pre.bind(arg), execution_profile=EXEC_PROFILE_COUNT).one()[0]

    def _Insert(self, key, columns, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        if cf_name is None and batch is None:
            raise VncError("one of cf_name or batch args "
                           "should be provided to insert {} for {}".format(
                               columns, key))
        if column_family:
            raise VncError("driver does not support column_family's arg "
                           "to insert {} for {}".format(columns, key))
        execute_batch = False
        if batch is None:
            # We use batch to handle multi inserts
            batch = self._Get_CF_Batch(cf_name)
            execute_batch = True
        for column, value in columns.items():
            cql = """
            INSERT INTO "{}"
            (key, column1, value)
            VALUES (textAsBlob(%s), textAsBlob(%s), %s)
            """.format(batch.cf_name)
            batch.add(cql, [six.text_type(key),
                            six.text_type(column),
                            six.text_type(value)])
        # In situation where 'batch' has been created locally we want
        # to execute it.
        if execute_batch:
            batch.send()

    def _Remove(self, key, columns=None, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        if cf_name is None and batch is None:
            raise VncError("one of cf_name or batch args "
                           "should be provided to remove {} for {}".format(
                               columns, key))
        if column_family:
            raise VncError("driver does not support column_family's arg "
                           "to remove {} for {}".format(columns, key))
        execute_batch = False
        if batch is None:
            # We use batch to handle multi removes.
            batch = self._Get_CF_Batch(cf_name)
            execute_batch = True
        if not columns:
            cql = """
            DELETE FROM {}
            WHERE key = textAsBlob(%s)
            """.format(batch.cf_name)
            batch.add(cql, [six.text_type(key)])
        else:
            for column in columns:
                cql = """
                DELETE FROM {}
                WHERE key = textAsBlob(%s)
                AND column1 = textAsBlob(%s)
                """.format(batch.cf_name)
                batch.add(cql, [six.text_type(key),
                                six.text_type(column)])
        # In situation where 'batch' has been created locally we want
        # to execute it.
        if execute_batch:
            batch.send()

    # TODO(sahid): Backward compatible function from thrift's driver.
    # Do we really need this?
    def _handle_exceptions(self, func, oper=None):
        def wrapper(*args, **kwargs):
            try:
                if self.get_status() != ConnectionStatus.UP:
                    self._Init_Cluster()
                self.start_time = datetime.datetime.now()
                return func(*args, **kwargs)
            except (cassandra.InvalidRequest,
                    cassandra.cluster.NoHostAvailable,
                    cassandra.cqlengine.CQLEngineException) as e:
                if self.get_status() != ConnectionStatus.DOWN:
                    self.report_status_down()
                    self.options.logger(
                        "Cassandra connection down. Exception in {}".format(func),
                        level=SandeshLevel.SYS_ERR)
                raise DatabaseUnavailableError(
                    "error, {}: {}".format(
                        e, utils.detailed_traceback()))
            finally:
                if ((self.log_response_time) and (oper)):
                    self.end_time = datetime.datetime.now()
                    self.log_response_time(self.end_time - self.start_time, oper)
        return wrapper
