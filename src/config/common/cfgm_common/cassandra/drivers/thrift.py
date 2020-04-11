from __future__ import absolute_import
from __future__ import unicode_literals
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
from builtins import next
from builtins import chr
from builtins import str
from builtins import range
from builtins import object
import copy

import six
import abc

import pycassa
from pycassa import ColumnFamily
from pycassa.batch import Mutator
from pycassa.system_manager import SystemManager, SIMPLE_STRATEGY
from pycassa.pool import AllServersUnavailable, MaximumRetryException
from pycassa.cassandra.ttypes import ConsistencyLevel
from pycassa.cassandra.ttypes import InvalidRequestException
import gevent

from vnc_api import vnc_api
from cfgm_common.exceptions import NoIdError, DatabaseUnavailableError, VncError
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns import constants as vns_constants
import time
from cfgm_common import jsonutils as json
from cfgm_common import utils
import datetime
import itertools
import sys
from collections import Mapping
from thrift.transport import TSSLSocket
import ssl
from cfgm_common.cassandra import api as cassa_api

def merge_dict(orig_dict, new_dict):
    for key, value in list(new_dict.items()):
        if key not in orig_dict:
            orig_dict[key] = new_dict[key]
        elif isinstance(value, Mapping):
            orig_dict[key] = merge_dict(orig_dict.get(key, {}), value)
        elif isinstance(value, list):
            orig_dict[key] = orig_dict[key].append(value)
        else:
            orig_dict[key] = new_dict[key]
    return orig_dict

class CassandraDriverThrift(cassa_api.CassandraDriver):

    _MAX_COL = 10000000

    def __init__(self, server_list, **options):
        super(CassandraDriverThrift, self).__init__(server_list, **options)

        self._reset_config = self.options.reset_config
        if self.options.db_prefix:
            self._db_prefix = '%s_' % (self.options.db_prefix)
        else:
            self._db_prefix = ''

        self._server_list = server_list
        if (self.options.pool_size == 0):
            self._pool_size = 2*(len(self._server_list))
        else:
            self._pool_size = self.options.pool_size

        self._num_dbnodes = len(self._server_list)
        self._conn_state = ConnectionStatus.INIT
        self._logger = self.options.logger
        self._credential = self.options.credential
        self.log_response_time = self.options.log_response_time
        self._ssl_enabled = self.options.ssl_enabled
        self._ca_certs = self.options.ca_certs

        # if no generate_url is specified, use a dummy function that always
        # returns an empty string
        self._generate_url = self.options.generate_url or (lambda x, y: '')
        self._cf_dict = {}
        self._ro_keyspaces = self.options.ro_keyspaces or {}
        self._rw_keyspaces = self.options.rw_keyspaces or {}
        if ((cassa_api.UUID_KEYSPACE_NAME not in self._ro_keyspaces) and
            (cassa_api.UUID_KEYSPACE_NAME not in self._rw_keyspaces)):
            self._ro_keyspaces.update(cassa_api.UUID_KEYSPACE)
        self._cassandra_init(server_list)
        if (((cassa_api.OBJ_SHARED_CF_NAME in self._ro_keyspaces.get(cassa_api.UUID_KEYSPACE_NAME, {}))) or
             (cassa_api.OBJ_SHARED_CF_NAME in self._rw_keyspaces.get(cassa_api.UUID_KEYSPACE_NAME, {}))):
            self._obj_shared_cf = self._cf_dict[cassa_api.OBJ_SHARED_CF_NAME]

        self.get_one_col = self._handle_exceptions(self.get_one_col)
        self.get_range = self._handle_exceptions(self.get_range)
    # end __init__

    # Helper routines for cassandra
    def _cassandra_init(self, server_list):
        # Ensure keyspace and schema/CFs exist

        self._update_sandesh_status(ConnectionStatus.INIT)

        ColumnFamily.get = self._handle_exceptions(ColumnFamily.get, "GET")
        ColumnFamily.multiget = self._handle_exceptions(ColumnFamily.multiget, "MULTIGET")
        ColumnFamily.xget = self._handle_exceptions(ColumnFamily.xget, "XGET")
        ColumnFamily.get_range = self._handle_exceptions(ColumnFamily.get_range, "GET_RANGE")
        ColumnFamily.insert = self._handle_exceptions(ColumnFamily.insert, "INSERT")
        ColumnFamily.remove = self._handle_exceptions(ColumnFamily.remove, "REMOVE")
        Mutator.send = self._handle_exceptions(Mutator.send, "SEND")

        self.sys_mgr = self._cassandra_system_manager()
        self.existing_keyspaces = self.sys_mgr.list_keyspaces()
        for ks, cf_dict in list(self._rw_keyspaces.items()):
            keyspace = '%s%s' % (self._db_prefix, ks)
            self._cassandra_ensure_keyspace(keyspace, cf_dict)

        for ks, _ in list(self._ro_keyspaces.items()):
            keyspace = '%s%s' % (self._db_prefix, ks)
            self._cassandra_wait_for_keyspace(keyspace)

        self._cassandra_init_conn_pools()
    # end _cassandra_init

    def _cassandra_system_manager(self):
        # Retry till cassandra is up
        server_idx = 0
        connected = False
        socket_factory = self._make_socket_factory()
        while not connected:
            try:
                cass_server = self._server_list[server_idx]
                sys_mgr = SystemManager(cass_server,
                                        credentials=self._credential,
                                        socket_factory=socket_factory)
                connected = True
            except Exception:
                # TODO do only for
                # thrift.transport.TTransport.TTransportException
                server_idx = (server_idx + 1) % self._num_dbnodes
                time.sleep(3)
        return sys_mgr
    # end _cassandra_system_manager

    def _cassandra_wait_for_keyspace(self, keyspace):
        # Wait for it to be created by another process
        while keyspace not in self.existing_keyspaces:
            gevent.sleep(1)
            self._logger("Waiting for keyspace %s to be created" % keyspace,
                         level=SandeshLevel.SYS_NOTICE)
            self.existing_keyspaces = self.sys_mgr.list_keyspaces()
    # end _cassandra_wait_for_keyspace

    def _cassandra_ensure_keyspace(self, keyspace_name, cf_dict):
        if self._reset_config and keyspace_name in self.existing_keyspaces:
            try:
                self.sys_mgr.drop_keyspace(keyspace_name)
            except InvalidRequestException as e:
                # TODO verify only EEXISTS
                self._logger(str(e), level=SandeshLevel.SYS_NOTICE)

        if (self._reset_config or keyspace_name not in self.existing_keyspaces):
            try:
                self.sys_mgr.create_keyspace(keyspace_name, SIMPLE_STRATEGY,
                        {'replication_factor': str(self._num_dbnodes)})
            except InvalidRequestException as e:
                # TODO verify only EEXISTS
                self._logger("Warning! " + str(e), level=SandeshLevel.SYS_WARN)

        gc_grace_sec = vns_constants.CASSANDRA_DEFAULT_GC_GRACE_SECONDS

        for cf_name in cf_dict:
            create_cf_kwargs = cf_dict[cf_name].get('create_cf_args', {})
            try:
                self.sys_mgr.create_column_family(
                    keyspace_name, cf_name,
                    gc_grace_seconds=gc_grace_sec,
                    default_validation_class='UTF8Type',
                    **create_cf_kwargs)
            except InvalidRequestException as e:
                # TODO verify only EEXISTS
                self._logger("Info! " + str(e), level=SandeshLevel.SYS_INFO)
                self.sys_mgr.alter_column_family(keyspace_name, cf_name,
                    gc_grace_seconds=gc_grace_sec,
                    default_validation_class='UTF8Type',
                    **create_cf_kwargs)
    # end _cassandra_ensure_keyspace

    def _make_ssl_socket_factory(self, ca_certs, validate=True):
        # copy method from pycassa library because no other method
        # to override ssl version
        def ssl_socket_factory(host, port):
            TSSLSocket.TSSLSocket.SSL_VERSION = ssl.PROTOCOL_TLSv1_2
            return TSSLSocket.TSSLSocket(host, port, ca_certs=ca_certs, validate=validate)
        return ssl_socket_factory

    def _make_socket_factory(self):
        socket_factory = pycassa.connection.default_socket_factory
        if self._ssl_enabled:
            socket_factory = self._make_ssl_socket_factory(
                self._ca_certs, validate=False)
        return socket_factory

    def _cassandra_init_conn_pools(self):
        socket_factory = self._make_socket_factory()
        for ks, cf_dict in itertools.chain(list(self._rw_keyspaces.items()),
                                           list(self._ro_keyspaces.items())):
            keyspace = '%s%s' % (self._db_prefix, ks)
            pool = pycassa.ConnectionPool(
                keyspace, self._server_list, max_overflow=5, use_threadlocal=True,
                prefill=True, pool_size=self._pool_size, pool_timeout=120,
                max_retries=15, timeout=5, credentials=self._credential,
                socket_factory=socket_factory)

            for cf_name in cf_dict:
                cf_kwargs = cf_dict[cf_name].get('cf_args', {})
                self._cf_dict[cf_name] = ColumnFamily(
                    pool,
                    cf_name,
                    read_consistency_level=ConsistencyLevel.QUORUM,
                    write_consistency_level=ConsistencyLevel.QUORUM,
                    dict_class=dict,
                    **cf_kwargs)

        ConnectionState.update(conn_type = ConnType.DATABASE,
            name = 'Cassandra', status = ConnectionStatus.UP, message = '',
            server_addrs = self._server_list)
        self._conn_state = ConnectionStatus.UP
        msg = 'Cassandra connection ESTABLISHED'
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
    # end _cassandra_init_conn_pools

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnType.DATABASE,
                               name='Cassandra', status=status, message=msg,
                               server_addrs=self._server_list)

    def _handle_exceptions(self, func, oper=None):
        def wrapper(*args, **kwargs):
            if (sys._getframe(1).f_code.co_name != 'multiget' and
                    func.__name__ in ['get', 'multiget']):
                msg = ("It is not recommended to use 'get' or 'multiget' "
                       "pycassa methods. It's better to use 'xget' or "
                       "'get_range' methods due to thrift limitations")
                self._logger(msg, level=SandeshLevel.SYS_WARN)
            try:
                if self._conn_state != ConnectionStatus.UP:
                    # will set conn_state to UP if successful
                    self._cassandra_init_conn_pools()

                self.start_time = datetime.datetime.now()
                return func(*args, **kwargs)
            except (AllServersUnavailable, MaximumRetryException) as e:
                if self._conn_state != ConnectionStatus.DOWN:
                    self._update_sandesh_status(ConnectionStatus.DOWN)
                    msg = 'Cassandra connection down. Exception in %s' % (
                        str(func))
                    self._logger(msg, level=SandeshLevel.SYS_ERR)

                self._conn_state = ConnectionStatus.DOWN
                raise DatabaseUnavailableError(
                    'Error, %s: %s' % (str(e), utils.detailed_traceback()))

            finally:
                if ((self.log_response_time) and (oper)):
                    self.end_time = datetime.datetime.now()
                    self.log_response_time(self.end_time - self.start_time, oper)
        return wrapper
    # end _handle_exceptions

    def get_cf_batch(self, cf_name, keyspace_name=None):
        """Get batch object bind to a column family used in insert/remove"""
        return self.get_cf(cf_name).batch()

    def get_cf(self, cf_name):
        return self._cf_dict.get(cf_name)

    def get_range(self, cf_name):
        try:
            return self.get_cf(cf_name).get_range(
                         column_count=100000)
        except:
            return None

    def get_one_col(self, cf_name, key, column):
        col = self.multiget(cf_name, [key], columns=[column])
        if key not in col:
            raise NoIdError(key)
        elif len(col[key]) > 1:
            raise VncError('Multi match %s for %s' % (column, key))
        return col[key][column]

    def multiget(self, cf_name, keys, columns=None, start='', finish='',
                 timestamp=False, num_columns=None):
        _thrift_limit_size = 10000
        results = {}
        cf = self.get_cf(cf_name)

        # if requested, read lesser than default
        if num_columns and num_columns < self._MAX_COL:
            column_count = num_columns
        else:
            column_count = self._MAX_COL

        if not columns or start or finish:
            try:
                results = cf.multiget(keys,
                                      column_start=start,
                                      column_finish=finish,
                                      include_timestamp=timestamp,
                                      column_count=column_count)
            except OverflowError:
                for key in keys:
                    rows = dict(cf.xget(key,
                                        column_start=start,
                                        column_finish=finish,
                                        include_timestamp=timestamp))
                    if rows:
                        results[key] = rows

            empty_keys = [key for key, value in list(results.items()) if not value]
            if empty_keys:
                msg = ("Multiget for %d keys returned with an empty value "
                       "CF (%s): Empty Keys (%s), columns (%s), start (%s), "
                       "finish (%s). Retrying with xget" % (len(keys), cf_name,
                           empty_keys, columns, start, finish))
                self._logger(msg, level=SandeshLevel.SYS_DEBUG)
                # CEM-8595; some rows are None. fall back to xget
                for key in empty_keys:
                    rows = dict(cf.xget(key,
                                        column_start=start,
                                        column_finish=finish,
                                        include_timestamp=timestamp))
                    if rows:
                        results[key] = rows

        if columns:
            max_key_range, _ = divmod(_thrift_limit_size, len(columns))
            if max_key_range > 1:
                for key_chunk in [keys[x:x+max_key_range] for x in
                                  range(0, len(keys), max_key_range)]:
                    rows = cf.multiget(key_chunk,
                                       columns=columns,
                                       include_timestamp=timestamp,
                                       column_count=column_count)
                    merge_dict(results, rows)
            elif max_key_range == 0:
                for column_chunk in [columns[x:x+(_thrift_limit_size - 1)] for x in
                                     range(0, len(columns), _thrift_limit_size - 1)]:
                    rows = cf.multiget(keys,
                                       columns=column_chunk,
                                       include_timestamp=timestamp,
                                       column_count=column_count)
                    merge_dict(results, rows)
            elif max_key_range == 1:
                for key in keys:
                    try:
                        cols = cf.get(key,
                                      columns=column_chunk,
                                      include_timestamp=timestamp,
                                      column_count=column_count)
                    except pycassa.NotFoundException:
                        continue
                    results.setdefault(key, {}).update(cols)

        empty_row_keys = []
        for key in results:
            # https://bugs.launchpad.net/juniperopenstack/+bug/1712905
            # Probably due to concurrency access to the DB when a resource is
            # deleting, pycassa could return key without value, ignore it
            if results[key] is None:
                msg = ("Multiget result contains a key (%s) with an empty "
                       "value. %s: number of keys (%d), columns (%s), start (%s), "
                       "finish (%s)" % (key, cf_name, len(keys), columns, start,
                                        finish))
                self._logger(msg, level=SandeshLevel.SYS_WARN)
                empty_row_keys.append(key)
                continue

            # only CF of config_db_uuid keyspace JSON encode its column values
            if not cf._cfdef.keyspace.endswith(cassa_api.UUID_KEYSPACE_NAME):
                continue

            for col, val in list(results[key].items()):
                try:
                    if timestamp:
                        results[key][col] = (json.loads(val[0]), val[1])
                    else:
                        results[key][col] = json.loads(val)
                except (ValueError, TypeError) as e:
                    msg = ("Cannot json load the value of cf: %s, key:%s "
                           "(error: %s). Use it as is: %s" %
                           (cf_name, key, str(e),
                            val if not timestamp else val[0]))
                    self._logger(msg, level=SandeshLevel.SYS_INFO)
                    results[key][col] = val

        for row_key in empty_row_keys:
            del results[row_key]
        return results

    def get(self, cf_name, key, columns=None, start='', finish=''):
        result = self.multiget(cf_name,
                               [key],
                               columns=columns,
                               start=start,
                               finish=finish)
        return result.get(key)

    def insert(self, key, columns, keyspace_name=None, cf_name=None,
               batch=None, column_famiy=None):
        """Insert columns with value in a row in a column family"""

        if batch:
            batch.insert(key, columns)
        elif column_family:
            column_famlily.insert(key, columns)
        elif cf_name:
            self._cf_dict[cf_name].insert(key, columns)
        else:
            raise VncError('No cf or batch for db insert %s for %s'
                % (columns, key))

    def remove(self, key, columns=None, keyspace_name=None, cf_name=None,
               batch=None, column_famiy=None):
        """Remove a specified row or a set of columns within the row"""

        if batch:
            batch.remove(key, columns)
        elif column_family:
            column_famlily.remove(key, columns)
        elif cf_name:
            self._cf_dict[cf_name].remove(key, columns)
        else:
            raise VncError('No cf or batch for db remove %s for %s'
                % (columns, key))

