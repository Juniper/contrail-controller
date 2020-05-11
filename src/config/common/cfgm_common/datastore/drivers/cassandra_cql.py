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

try:
    connector = importlib.import_module('cassandra')
    connector.cluster = importlib.import_module('cassandra.cluster')
    connector.query = importlib.import_module('cassandra.query')
    connector.cqltypes = importlib.import_module('cassandra.cqltypes')
    connector.protocol = importlib.import_module('cassandra.protocol')
    connector.cqlengine = importlib.import_module('cassandra.cqlengine')
    connector.cqlengine.models = importlib.import_module('cassandra.cqlengine.models')
    connector.cqlengine.columns = importlib.import_module('cassandra.cqlengine.columns')
    connector.cqlengine.management = importlib.import_module('cassandra.cqlengine.management')
except ImportError as e:
    connector = None

from cfgm_common.datastore import api as datastore_api
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

# SSL related configurations
if sys.version_info >= (3, 6):
    # Python3.6 Introduces PROTOCOL_TLS which is using the best
    # version supported.
    SSL_VERSION = ssl.PROTOCOL_TLS
else:
    SSL_VERSION = ssl.PROTOCOL_TLSv1_2

# Set hard limit of the columns returned.
MAX_COLUMNS = 10000000

# Primitive used to encapsulate result of a row
#RowResultType = collections.OrderedDict
RowResultType = dict

# String will be encoded in UTF-8 if mecessary (Python2 support)
StringType = six.text_type

# Decodes JSON string in Python Object.
JsonToObject = lambda x: json.loads(x)


# This is encapsulating the ResultSet iterator that to provide feature
# to filter the columns, decode JSON or add timestamp.
class Iter(six.Iterator):
    def __init__(self, result, columns,
                 include_timestamp=False,
                 decode_json=True,
                 limit=MAX_COLUMNS):
        # Based on a `connector.ResulSet` or a `list`.
        self.result = result

        # Defines `columns` wanted. `columns_found` we help to track
        # the columns alredy discovered during interation of the
        # result so we can stop early.
        self.columns = columns and set(columns) or set([])
        self.columns_found = set([])

        # Based on Pycassa ResultSet (legacy code) the results will be
        # of type {column: value} or {column: (value, timestamp)}
        self.include_timestamp = include_timestamp
        # When `True`, the JSON string in `value` will be decoded.
        self.decode_json = decode_json

        # Based on legacy it looks like we have hard limit, not sure
        # if that still make sense.
        self.limit = min(MAX_COLUMNS, limit)

        # Internal properties used to iterate result and keep track of
        # the number of iterations.
        self.it = iter(result)
        self.it_step = 0

    __iter__ = lambda self: self
    __next__ = lambda self: self.next()

    def append(self, value):
        self.result.append(value)
        # Iterator changed size
        self.it = iter(self.result)
        self.it_step = 0

    def decode(self, v):
        if self.decode_json:
            return JsonToObject(v)
        return v

    def timestamp(self, v, w):
        if self.include_timestamp:
            return (v, w)
        return v

    def format_value(self, v, w):
        return self.timestamp(self.decode(v), w)

    def get_next_items(self):
        if self.columns and self.columns_found == self.columns:
            # If we have found all the columns needed, no need to
            # continue.
            raise StopIteration

        self.it_step += 1
        if self.it_step <= self.limit:
            return next(self.it)
        raise StopIteration

    def next(self):
        column, value, timestamp = self.get_next_items()
        if self.columns and (column not in self.columns):
            return next(self)
        if self.columns:
            # Keep track of the columns found so we can stop early the
            # iteration.
            self.columns_found.add(column)
        return column, self.format_value(value, timestamp)

    def one(self):
        try:
            return next(self)
        except StopIteration:
            return tuple()

    def all(self):
        return RowResultType(
            list(self))


# This is implementing our Cassandra API for the driver provided by
# DataStax, we try to stay simple, compatible with legacy, and finally
# compatible with Python2 as-well.  We have made the choice to avoid
# using the Object Mapping proposed by DataStax's driver to allow more
# flexibilities.  In a perfect world, any change here should be
# acompagned by an unittest to avoid regression.
class CassandraDriverCQL(datastore_api.CassandraDriver):


    def __init__(self, server_list, **options):
        global connector
        if connector is None:
            raise ImportError("the CQL connector is not defined, can't "
                              "be null at this step. Please verify "
                              "dependencies.")
        super(CassandraDriverCQL, self).__init__(server_list, **options)

    @property
    def ConsistencyLevel(self):
        # Setting that defines a successful write or read by the
        # number of cluster replicas that acknowledge the write or
        # respond to the read request, respectively. e.g: QUORUM #
        # ceil(RF/2) replicas must respond to consider the operation a
        # success
        return connector.ConsistencyLevel.QUORUM

    @property
    def BatchType(self):
        # Because we use `Batch` with queries within same CFs and
        # partition keys we can avoid writting LOG, that is improving
        # performance.
        return connector.query.BatchType.UNLOGGED

    @property
    def CqlVersion(self):
        # None means the best version supported by servers will be
        # used.
        return None

    @property
    def ProtocolVersions(self):
        # The connector will try to connect server from the higher
        # version to the lower. The minimun supported Cassandra is
        # 2.0.
        return (4, 3, 2)

    @property
    def BatchClass(self):
        # This is encapsulating `Batch` statment of DataStax connector that to
        # make it to have same behavior of `Batch` type form pycassa which is
        # what legacy is using. It also add check related to performance like
        # having a batch executed for different partition key or executing for
        # a same partition key an insert/remove.
        class Batch(connector.query.BatchStatement):
            def __init__(self, context, cf_name, logger):
                self.cf_name = cf_name
                self.partition_key = None
                self.action = None
                self.context = context
                super(Batch, self).__init__(
                    session=context.get_cf(cf_name),
                    consistency_level=self.context.ConsistencyLevel,
                    batch_type=self.context.BatchType)

            def is_same_partition_key(self, partition_key):
                if self.partition_key is None:
                    self.partition_key = partition_key
                return self.partition_key == partition_key

            def is_same_action(self, action):
                if self.action is None:
                    self.action = action
                return self.action == action

            def add_insert(self, *args, **kwargs):
                return self.add("insert", *args, **kwargs)

            def add_remove(self, *args, **kwargs):
                return self.add("remove", *args, **kwargs)

            def add(self, action, partition_key, *args, **kwargs):
                """Add query to the batch"""
                logger = self.context.options.logger
                if not self.is_same_partition_key(partition_key):
                    logger("Adding in `batch` a query using "
                           "different partition keys, this implies "
                           "performance degration, commiting "
                           "current batch. (prev={}, new={})".format(
                               self.partition_key, partition_key),
                           level=SandeshLevel.SYS_DEBUG)
                    self.send()
                    self.partition_key = partition_key
                elif self.is_same_action(action):
                    logger("Adding in `batch` a query using "
                           "insert/delete with the same partition keys, this is "
                           "not supported by CQL (prev={}, new={})".format(
                               self.action, action),
                           level=SandeshLevel.SYS_DEBUG)
                    self.send()
                    self.action = action
                return super(Batch, self).add(*args, **kwargs)

            def send(self):
                """Commits batch and clear statement for new usage"""
                self._session.execute(self)
                self.clear()

        if self.__batch_class__ is None:
            self.__batch_class__ = Batch
        return self.__batch_class__

    __batch_class__ = None

    @property
    def RowFactory(self):
        return lambda c, r: r

    def _Init_Cluster(self):
        self.report_status_init()

        # Authentication related options
        auth_provider = None
        if self.options.credential:
            auth_provider = connector.auth.PlainTextAuthProvider(
                username=self.options.credential.get('username'),
                password=self.options.credential.get('password'))

        # SSL related options
        ssl_options, ssl_context = None, None
        if self.options.ssl_enabled:
            ssl_context = ssl.SSLContext(SSL_VERSION)
            ssl_context.load_verify_locations(self.options.ca_certs)
            ssl_context.verify_mode = ssl.CERT_REQUIRED
            ssl_context.check_hostname = False
            ssl_options = {}

        # Profiles related features
        profiles = {
            connector.cluster.EXEC_PROFILE_DEFAULT: connector.cluster.ExecutionProfile(
                # TODO(sahid): Do we really want QUORUM when counting?
                consistency_level=self.ConsistencyLevel,
                row_factory=self.RowFactory),
        }

        # Addresses, ports related options
        endpoints, port = [], connector.cluster.Cluster.port
        for address in self._server_list:
            try:
                server, port = address.split(':', 1)
                # Warning: Only the last port will be used, this
                # connector does not support multiple ports. In all
                # cases it is not necessary to refer all the servers,
                # there is an internal system to discover Cassandra's
                # server.
                endpoints.append(server)
            except ValueError:
                endpoints.append(address)

        connector.ProtocolVersion.SUPPORTED_VERSIONS = self.ProtocolVersions
        try:
            self._cluster = connector.cluster.Cluster(
                endpoints, port=int(port),
                ssl_options=ssl_options,
                ssl_context=ssl_context,
                auth_provider=auth_provider,
                execution_profiles=profiles,
                cql_version=self.CqlVersion)
            self._cluster.connect()
        except Exception as error:
            raise DatabaseUnavailableError(
                "error, {}: {}".format(
                    error, utils.detailed_traceback()))

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
        except connector.cluster.NoHostAvailable:
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
        except connector.protocol.AlreadyExists:
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
        except connector.protocol.ConfigurationException:
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
        except connector.protocol.AlreadyExists:
            msg = "keyspace '{}', already created"
        self.options.logger(
            msg.format(keyspace), level=SandeshLevel.SYS_NOTICE)

    def _cql_select(self, cf_name, key, start='', finish='', limit=None,
                    columns=None, include_timestamp=False, decode_json=None):
        ses = self.get_cf(cf_name)
        arg, cql = [StringType(key)], """
        SELECT blobAsText(column1), value, WRITETIME(value)
        FROM "{}"
        WHERE key = textAsBlob(?)
        """.format(cf_name)
        if start:
            cql += "AND column1 >= textAsBlob(?) "
            arg.append(StringType(start))
        if finish:
            cql += "AND column1 <= textAsBlob(?) "
            arg.append(StringType(finish))
        if limit:
            cql += "LIMIT ? "
            arg.append(limit)
        pre = ses.prepare(cql)
        if decode_json is None:
            # Only the CFs related to UUID_KEYSPACE_NAME's keyspace
            # encode its column values to JSON we want decode them
            # automatically.
            decode_json = ses.keyspace.endswith(datastore_api.UUID_KEYSPACE_NAME)
        return Iter(ses.execute(pre.bind(arg)),
                    # Filtering the columns using cassandra adds
                    # performance degradation, letting Python
                    # processes doing that job locally, see: ALLOW
                    # FILTERING.
                    columns=columns,
                    include_timestamp=include_timestamp,
                    decode_json=decode_json)

    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        return self.BatchClass(context=self,
                               cf_name=cf_name,
                               logger=self.options.logger)

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
                limit=num_columns).all()
            if row:
                # We should have used a generator but legacy does not
                # handle it.
                res[key] = row
        return res

    def _XGet(self, cf_name, key, columns=None, start='', finish=''):
        try:
            return self._cql_select(
                cf_name=cf_name,
                key=key,
                start=start,
                finish=finish,
                columns=columns,
                # TODO(sahid): In legacy, the implementation of `xget`
                # does not use `multiget` so does not decode the JSON
                # string in Python Object for keyspace
                # UUID_KEYSPACE_NAME. We should try to be coherent.
                decode_json=False)
        except StopIteration:
            # Looks like the legacy is expecting an empty iterator.
            return iter([])

    def _Get(self, cf_name, key, columns=None, start='', finish=''):
        return self._cql_select(
            cf_name=cf_name,
            key=key,
            start=start,
            finish=finish,
            columns=columns).all() or None

    def _Get_One_Col(self, cf_name, key, column):
        itr = self._cql_select(cf_name=cf_name, key=key, columns=[column])
        res = itr.all()
        if not res:
            # Looks like legacy is expecting an exception.
            raise NoIdError(key)
        return res[column]

    def _Get_Range(self, cf_name, columns=None, column_count=100000):
        ses = self.get_cf(cf_name)
        res, arg, cql = {}, [column_count], """
        SELECT blobAsText(key), blobAsText(column1), value, WRITETIME(value)
        FROM "{}"
        LIMIT ?
        """.format(cf_name)
        pre = ses.prepare(cql)
        # TODO(sahid): If we could GROUP BY key we could probably
        # avoid this loop.
        for key, column, value, timestamp in ses.execute(pre.bind(arg)):
            res.setdefault(key, Iter(
                [],
                # Filtering the columns using cassandra adds
                # performance degradation, letting Python processes
                # doing that job locally, see: ALLOW FILTERING.
                columns=columns,
                # TODO(sahid): In legacy, the implementation of
                # `get_range` does not use `multiget` so does not
                # decode the JSON string in Python Object for keyspace
                # UUID_KEYSPACE_NAME. We should try to be coherent.
                decode_json=False)).append((column, value, timestamp))
        for key, iterable in res.items():
            row = iterable.all()
            if row:
                yield key, row

    def _Get_Count(self, cf_name, key, start='', finish='', keyspace_name=None):
        ses = self.get_cf(cf_name)
        arg, cql = [], """
          SELECT COUNT(*) FROM "{}"
          WHERE key = textAsBlob(?)
        """.format(cf_name)
        arg = [StringType(key)]
        if start:
            cql += "AND column1 >= textAsBlob(?) "
            arg.append(StringType(start))
        if finish:
            cql += "AND column1 <= textAsBlob(?) "
            arg.append(StringType(finish))
        pre = ses.prepare(cql)
        return ses.execute(
            pre.bind(arg)).one()[0]

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
            batch.add_insert(key, cql, [StringType(key),
                                        StringType(column),
                                        StringType(value)])
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
            batch.add_remove(key, cql, [StringType(key)])
        else:
            for column in columns:
                cql = """
                DELETE FROM {}
                WHERE key = textAsBlob(%s)
                AND column1 = textAsBlob(%s)
                """.format(batch.cf_name)
                batch.add_remove(key, cql, [StringType(key),
                                            StringType(column)])
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
            except (connector.InvalidRequest,
                    connector.cluster.NoHostAvailable,
                    connector.cqlengine.CQLEngineException) as e:
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
