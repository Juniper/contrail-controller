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
import importlib

import six
import abc

try:
    pycassa = importlib.import_module('pycassa')
    pycassa.batch = importlib.import_module('pycassa.batch')
    pycassa.system_manager = importlib.import_module('pycassa.system_manager')
    pycassa.cassandra = importlib.import_module('pycassa.cassandra')
    pycassa.cassandra.ttypes = importlib.import_module('pycassa.cassandra.ttypes')
    pycassa.pool = importlib.import_module('pycassa.pool')
    transport = importlib.import_module('thrift.transport')
except ImportError:
    pycassa, transport = None, None

import gevent

from vnc_api import vnc_api
from cfgm_common.exceptions import NoIdError, DatabaseUnavailableError, VncError
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns import constants as vns_constants
import time
from cfgm_common import jsonutils as json
from cfgm_common import utils
import datetime
import itertools
import sys
from collections import Mapping
import ssl
from cfgm_common.datastore.api import CassandraDriver
from cfgm_common.datastore import api as datastore_api


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

class CassandraDriverThrift(datastore_api.CassandraDriver):

    _MAX_COL = 10000000

    def __init__(self, server_list, **options):
        global pycassa, transport
        if pycassa is None or transport is None:
            raise ImportError("The variables 'pycassa' and 'tranport' can't "
                              "be null at this step. Please verify "
                              "dependencies.")

        super(CassandraDriverThrift, self).__init__(server_list, **options)

        self._cf_dict = {}
        if ((datastore_api.UUID_KEYSPACE_NAME not in self.options.ro_keyspaces) and
            (datastore_api.UUID_KEYSPACE_NAME not in self.options.rw_keyspaces)):
            self.options.ro_keyspaces.update(datastore_api.UUID_KEYSPACE)
        self._cassandra_init(server_list)

        self.get_one_col = self._handle_exceptions(self.get_one_col)
        self.get_range = self._handle_exceptions(self.get_range)
    # end __init__

    # Helper routines for cassandra
    def _cassandra_init(self, server_list):
        # Ensure keyspace and schema/CFs exist

        self.report_status_init()

        pycassa.ColumnFamily.get = self._handle_exceptions(pycassa.ColumnFamily.get, "GET")
        pycassa.ColumnFamily.multiget = self._handle_exceptions(pycassa.ColumnFamily.multiget, "MULTIGET")
        pycassa.ColumnFamily.xget = self._handle_exceptions(pycassa.ColumnFamily.xget, "XGET")
        pycassa.ColumnFamily.get_range = self._handle_exceptions(pycassa.ColumnFamily.get_range, "GET_RANGE")
        pycassa.ColumnFamily.insert = self._handle_exceptions(pycassa.ColumnFamily.insert, "INSERT")
        pycassa.ColumnFamily.remove = self._handle_exceptions(pycassa.ColumnFamily.remove, "REMOVE")
        pycassa.batch.Mutator.send = self._handle_exceptions(pycassa.batch.Mutator.send, "SEND")

        self.sys_mgr = self._cassandra_system_manager()
        self.existing_keyspaces = self.sys_mgr.list_keyspaces()
        for ks, cf_dict in list(self.options.rw_keyspaces.items()):
            keyspace = self.keyspace(ks)
            self._cassandra_ensure_keyspace(keyspace, cf_dict)

        for ks, _ in list(self.options.ro_keyspaces.items()):
            keyspace = self.keyspace(ks)
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
                sys_mgr = pycassa.system_manager.SystemManager(cass_server,
                                        credentials=self.options.credential,
                                        socket_factory=socket_factory)
                connected = True
            except Exception:
                # TODO do only for
                # thrift.transport.TTransport.TTransportException
                server_idx = (server_idx + 1) % self.nodes()
                time.sleep(3)
        return sys_mgr
    # end _cassandra_system_manager

    def _cassandra_wait_for_keyspace(self, keyspace):
        # Wait for it to be created by another process
        while keyspace not in self.existing_keyspaces:
            gevent.sleep(1)
            self.options.logger("Waiting for keyspace %s to be created" % keyspace,
                         level=SandeshLevel.SYS_NOTICE)
            self.existing_keyspaces = self.sys_mgr.list_keyspaces()
    # end _cassandra_wait_for_keyspace

    def _cassandra_ensure_keyspace(self, keyspace_name, cf_dict):
        if self.options.reset_config and keyspace_name in self.existing_keyspaces:
            try:
                self.sys_mgr.drop_keyspace(keyspace_name)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                self.options.logger(str(e), level=SandeshLevel.SYS_NOTICE)

        if (self.options.reset_config or keyspace_name not in self.existing_keyspaces):
            try:
                self.sys_mgr.create_keyspace(keyspace_name, pycassa.system_manager.SIMPLE_STRATEGY,
                        {'replication_factor': str(self.nodes())})
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                self.options.logger("Warning! " + str(e), level=SandeshLevel.SYS_WARN)

        gc_grace_sec = vns_constants.CASSANDRA_DEFAULT_GC_GRACE_SECONDS

        for cf_name in cf_dict:
            create_cf_kwargs = cf_dict[cf_name].get('create_cf_args', {})
            try:
                self.sys_mgr.create_column_family(
                    keyspace_name, cf_name,
                    gc_grace_seconds=gc_grace_sec,
                    default_validation_class='UTF8Type',
                    **create_cf_kwargs)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                self.options.logger("Info! " + str(e), level=SandeshLevel.SYS_INFO)
                self.sys_mgr.alter_column_family(keyspace_name, cf_name,
                    gc_grace_seconds=gc_grace_sec,
                    default_validation_class='UTF8Type',
                    **create_cf_kwargs)
    # end _cassandra_ensure_keyspace

    def _make_ssl_socket_factory(self, ca_certs, validate=True):
        # copy method from pycassa library because no other method
        # to override ssl version
        def ssl_socket_factory(host, port):
            transport.TSSLSocket.TSSLSocket.SSL_VERSION = ssl.PROTOCOL_TLSv1_2
            return transport.TSSLSocket.TSSLSocket(host, port, ca_certs=ca_certs, validate=validate)
        return ssl_socket_factory

    def _make_socket_factory(self):
        socket_factory = pycassa.connection.default_socket_factory
        if self.options.ssl_enabled:
            socket_factory = self._make_ssl_socket_factory(
                self.options.ca_certs, validate=False)
        return socket_factory

    def _cassandra_init_conn_pools(self):
        socket_factory = self._make_socket_factory()
        for ks, cf_dict in itertools.chain(list(self.options.rw_keyspaces.items()),
                                           list(self.options.ro_keyspaces.items())):
            keyspace = self.keyspace(ks)
            pool = pycassa.ConnectionPool(
                keyspace, self._server_list, max_overflow=5, use_threadlocal=True,
                prefill=True, pool_size=self.pool_size(), pool_timeout=120,
                max_retries=15, timeout=5, credentials=self.options.credential,
                socket_factory=socket_factory)

            for cf_name in cf_dict:
                cf_kwargs = cf_dict[cf_name].get('cf_args', {})
                self._cf_dict[cf_name] = pycassa.ColumnFamily(
                    pool,
                    cf_name,
                    read_consistency_level=pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM,
                    write_consistency_level=pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM,
                    dict_class=dict,
                    **cf_kwargs)

        self.report_status_up()
        msg = 'Cassandra connection ESTABLISHED'
        self.options.logger(msg, level=SandeshLevel.SYS_NOTICE)
    # end _cassandra_init_conn_pools

    def _handle_exceptions(self, func, oper=None):
        def wrapper(*args, **kwargs):
            if (sys._getframe(1).f_code.co_name != 'multiget' and
                    func.__name__ in ['get', 'multiget']):
                msg = ("It is not recommended to use 'get' or 'multiget' "
                       "pycassa methods. It's better to use 'xget' or "
                       "'get_range' methods due to thrift limitations")
                self.options.logger(msg, level=SandeshLevel.SYS_WARN)
            try:
                if self.get_status() != ConnectionStatus.UP:
                    # will set conn_state to UP if successful
                    self._cassandra_init_conn_pools()

                self.start_time = datetime.datetime.now()
                return func(*args, **kwargs)
            except (pycassa.pool.AllServersUnavailable, pycassa.pool.MaximumRetryException) as e:
                if self.get_status() != ConnectionStatus.DOWN:
                    self.report_status_down()
                    msg = 'Cassandra connection down. Exception in %s' % (
                        str(func))
                    self.options.logger(msg, level=SandeshLevel.SYS_ERR)
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

    def get_range(self, cf_name, columns=None, column_count=100000):
        try:
            return self.get_cf(cf_name).get_range(
                column_count=column_count,
                columns=columns)
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
                self.options.logger(msg, level=SandeshLevel.SYS_DEBUG)
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
                self.options.logger(msg, level=SandeshLevel.SYS_WARN)
                empty_row_keys.append(key)
                continue

            # only CF of config_db_uuid keyspace JSON encode its column values
            if not cf._cfdef.keyspace.endswith(datastore_api.UUID_KEYSPACE_NAME):
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
                    self.options.logger(msg, level=SandeshLevel.SYS_INFO)
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

    def xget(self, cf_name, key, columns=None, start='', finish=''):
        return self.get_cf(cf_name).xget(
            key, column_start=start, column_finish=finish)

    def get_count(self, cf_name, key, start='', finish='', keyspace_name=None):
        return self.get_cf(cf_name).get_count(
            key, column_start=start, column_finish=finish)

    def insert(self, key, columns, keyspace_name=None, cf_name=None,
               batch=None, column_family=None):
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
               batch=None, column_family=None):
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

