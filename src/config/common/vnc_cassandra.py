#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import pycassa
from pycassa import ColumnFamily
from pycassa.batch import Mutator
from pycassa.system_manager import SystemManager, SIMPLE_STRATEGY
from pycassa.pool import AllServersUnavailable

from vnc_api.gen.vnc_cassandra_client_gen import VncCassandraClientGen
from exceptions import NoIdError
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import API_SERVER_KEYSPACE_NAME, \
    CASSANDRA_DEFAULT_GC_GRACE_SECONDS
import time
from cfgm_common import jsonutils as json
import utils

class VncCassandraClient(VncCassandraClientGen):
    # Name to ID mapping keyspace + tables
    _UUID_KEYSPACE_NAME = API_SERVER_KEYSPACE_NAME

    # TODO describe layout
    _OBJ_UUID_CF_NAME = 'obj_uuid_table'

    # TODO describe layout
    _OBJ_FQ_NAME_CF_NAME = 'obj_fq_name_table'

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._UUID_KEYSPACE_NAME, [cls._OBJ_UUID_CF_NAME,
                                              cls._OBJ_FQ_NAME_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, server_list, reset_config, db_prefix, keyspaces, logger,
                 generate_url=None):
        super(VncCassandraClient, self).__init__()
        self._reset_config = reset_config
        self._cache_uuid_to_fq_name = {}
        if db_prefix:
            self._db_prefix = '%s_' %(db_prefix)
        else:
            self._db_prefix = ''
        self._server_list = server_list
        self._conn_state = ConnectionStatus.INIT
        self._logger = logger

        # if no generate_url is specified, use a dummy function that always
        # returns an empty string
        self._generate_url = generate_url or (lambda x,y: '')
        self._cf_dict = {}
        self._keyspaces = {
            self._UUID_KEYSPACE_NAME: [(self._OBJ_UUID_CF_NAME, None),
                                       (self._OBJ_FQ_NAME_CF_NAME, None)]}

        if keyspaces:
            self._keyspaces.update(keyspaces)
        self._cassandra_init(server_list)
        self._cache_uuid_to_fq_name = {}
        self._obj_uuid_cf = self._cf_dict[self._OBJ_UUID_CF_NAME]
        self._obj_fq_name_cf = self._cf_dict[self._OBJ_FQ_NAME_CF_NAME]
    # end __init__

    def get_cf(self, func):
        return self._cf_dict.get(func)
    #end

    def add(self, func, key, value):
        try:
            self.get_cf(func).insert(key, value)
            return True
        except:
            return False
    #end

    def get(self, func, key):
        try:
            return self.get_cf(func).get(key)
        except:
            return None
    #end

    def delete(self, func, key):
        try:
            self.get_cf(func).remove(key)
            return True
        except:
            return False
    #end

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnectionType.DATABASE,
            name='Cassandra', status=status, message=msg,
            server_addrs=self._server_list)

    def _handle_exceptions(self, func):
        def wrapper(*args, **kwargs):
            try:
                if self._conn_state != ConnectionStatus.UP:
                    # will set conn_state to UP if successful
                    self._cassandra_init_conn_pools()

                return func(*args, **kwargs)
            except AllServersUnavailable:
                if self._conn_state != ConnectionStatus.DOWN:
                    self._update_sandesh_status(ConnectionStatus.DOWN)
                    msg = 'Cassandra connection down. Exception in %s' \
                          %(str(func))
                    self._logger(msg, level=SandeshLevel.SYS_ERR)

                self._conn_state = ConnectionStatus.DOWN
                raise

        return wrapper
    # end _handle_exceptions

    # Helper routines for cassandra
    def _cassandra_init(self, server_list):
        # 1. Ensure keyspace and schema/CFs exist
        # 2. Read in persisted data and publish to ifmap server

        self._update_sandesh_status(ConnectionStatus.INIT)
        
        ColumnFamily.get = self._handle_exceptions(ColumnFamily.get)
        ColumnFamily.multiget = self._handle_exceptions(ColumnFamily.multiget)
        ColumnFamily.xget = self._handle_exceptions(ColumnFamily.xget)
        ColumnFamily.get_range = self._handle_exceptions(ColumnFamily.get_range)
        ColumnFamily.insert = self._handle_exceptions(ColumnFamily.insert)
        ColumnFamily.remove = self._handle_exceptions(ColumnFamily.remove)
        Mutator.send = self._handle_exceptions(Mutator.send)

        for ks,cf_list in self._keyspaces.items():
            keyspace = '%s%s' %(self._db_prefix, ks)
            self._cassandra_ensure_keyspace(server_list, keyspace, cf_list)

        self._cassandra_init_conn_pools()
    # end _cassandra_init

    def _cassandra_ensure_keyspace(self, server_list,
                                   keyspace_name, cf_info_list):
        # Retry till cassandra is up
        server_idx = 0
        num_dbnodes = len(self._server_list)
        connected = False
        while not connected:
            try:
                cass_server = self._server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                # TODO do only for
                # thrift.transport.TTransport.TTransportException
                server_idx = (server_idx + 1) % num_dbnodes
                time.sleep(3)

        if self._reset_config:
            try:
                sys_mgr.drop_keyspace(keyspace_name)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                self._logger("Warning! " + str(e), level=SandeshLevel.SYS_WARN)

        try:
            sys_mgr.create_keyspace(keyspace_name, SIMPLE_STRATEGY,
                                    {'replication_factor': str(num_dbnodes)})
        except pycassa.cassandra.ttypes.InvalidRequestException as e:
            # TODO verify only EEXISTS
            self._logger("Warning! " + str(e), level=SandeshLevel.SYS_WARN)

        gc_grace_sec = CASSANDRA_DEFAULT_GC_GRACE_SECONDS

        for cf_info in cf_info_list:
            try:
                (cf_name, comparator_type) = cf_info
                if comparator_type:
                    sys_mgr.create_column_family(
                        keyspace_name, cf_name,
                        comparator_type=comparator_type,
                        gc_grace_seconds=gc_grace_sec,
                        default_validation_class='UTF8Type')
                else:
                    sys_mgr.create_column_family(keyspace_name, cf_name,
                        gc_grace_seconds=gc_grace_sec,
                        default_validation_class='UTF8Type')
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                self._logger("Warning! " + str(e), level=SandeshLevel.SYS_WARN)
                sys_mgr.alter_column_family(keyspace_name, cf_name,
                    gc_grace_seconds=gc_grace_sec,
                    default_validation_class='UTF8Type')
    # end _cassandra_ensure_keyspace

    def _cassandra_init_conn_pools(self):
        for ks,cf_list in self._keyspaces.items():
            pool = pycassa.ConnectionPool(
                ks, self._server_list, max_overflow=-1, use_threadlocal=True,
                prefill=True, pool_size=20, pool_timeout=120,
                max_retries=-1, timeout=5)

            rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
            wr_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
            
            for (cf, _) in cf_list:
                self._cf_dict[cf] = ColumnFamily(
                    pool, cf, read_consistency_level = rd_consistency,
                    write_consistency_level = wr_consistency)
    
        ConnectionState.update(conn_type = ConnectionType.DATABASE,
            name = 'Cassandra', status = ConnectionStatus.UP, message = '',
            server_addrs = self._server_list)
        self._conn_state = ConnectionStatus.UP
        msg = 'Cassandra connection ESTABLISHED'
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
    # end _cassandra_init_conn_pools

    def cache_uuid_to_fq_name_add(self, id, fq_name, obj_type):
        self._cache_uuid_to_fq_name[id] = (fq_name, obj_type)
    # end cache_uuid_to_fq_name_add

    def cache_uuid_to_fq_name_del(self, id):
        try:
            del self._cache_uuid_to_fq_name[id]
        except KeyError:
            pass
    # end cache_uuid_to_fq_name_del

    def uuid_to_fq_name(self, id):
        try:
            return self._cache_uuid_to_fq_name[id][0]
        except KeyError:
            try:
                obj = self._obj_uuid_cf.get(id, columns=['fq_name', 'type'])
            except pycassa.NotFoundException:
                raise NoIdError(id)

            fq_name = json.loads(obj['fq_name'])
            obj_type = json.loads(obj['type'])
            self.cache_uuid_to_fq_name_add(id, fq_name, obj_type)
            return fq_name
    # end uuid_to_fq_name

    def uuid_to_obj_type(self, id):
        try:
            return self._cache_uuid_to_fq_name[id][1]
        except KeyError:
            try:
                obj = self._obj_uuid_cf.get(id, columns=['fq_name', 'type'])
            except pycassa.NotFoundException:
                raise NoIdError(id)

            fq_name = json.loads(obj['fq_name'])
            obj_type = json.loads(obj['type'])
            self.cache_uuid_to_fq_name_add(id, fq_name, obj_type)
            return obj_type
    # end uuid_to_obj_type


    def fq_name_to_uuid(self, obj_type, fq_name):
        method_name = obj_type.replace('-', '_')
        fq_name_str = ':'.join(fq_name)
        col_start = '%s:' % (utils.encode_string(fq_name_str))
        col_fin = '%s;' % (utils.encode_string(fq_name_str))
        try:
            col_info_iter = self._obj_fq_name_cf.xget(
                method_name, column_start=col_start, column_finish=col_fin)
        except pycassa.NotFoundException:
            raise NoIdError('%s %s' % (obj_type, fq_name))

        col_infos = list(col_info_iter)

        if len(col_infos) == 0:
            raise NoIdError('%s %s' % (obj_type, fq_name))

        for (col_name, col_val) in col_infos:
            obj_uuid = col_name.split(':')[-1]

        return obj_uuid
    # end fq_name_to_uuid

    def _read_child(self, result, obj_uuid, child_type,
                    child_uuid, child_tstamp):
        if '%ss' % (child_type) not in result:
            result['%ss' % (child_type)] = []

        child_info = {}
        child_info['to'] = self.uuid_to_fq_name(child_uuid)
        child_info['href'] = self._generate_url(child_type, child_uuid)
        child_info['uuid'] = child_uuid
        child_info['tstamp'] = child_tstamp

        result['%ss' % (child_type)].append(child_info)
    # end _read_child

    def _read_ref(self, result, obj_uuid, ref_type, ref_uuid, ref_data_json):
        if '%s_refs' % (ref_type) not in result:
            result['%s_refs' % (ref_type)] = []

        ref_data = json.loads(ref_data_json)
        ref_info = {}
        try:
            ref_info['to'] = self.uuid_to_fq_name(ref_uuid)
        except NoIdError as e:
            ref_info['to'] = ['ERROR']

        if ref_data:
            try:
                ref_info['attr'] = ref_data['attr']
            except KeyError:
                # TODO remove backward compat old format had attr directly
                ref_info['attr'] = ref_data

        ref_info['href'] = self._generate_url(ref_type, ref_uuid)
        ref_info['uuid'] = ref_uuid

        result['%s_refs' % (ref_type)].append(ref_info)
    # end _read_ref

    def _read_back_ref(self, result, obj_uuid, back_ref_type,
                       back_ref_uuid, back_ref_data_json):
        if '%s_back_refs' % (back_ref_type) not in result:
            result['%s_back_refs' % (back_ref_type)] = []

        back_ref_info = {}
        back_ref_info['to'] = self.uuid_to_fq_name(back_ref_uuid)
        back_ref_data = json.loads(back_ref_data_json)
        if back_ref_data:
            try:
                back_ref_info['attr'] = back_ref_data['attr']
            except KeyError:
                # TODO remove backward compat old format had attr directly
                back_ref_info['attr'] = back_ref_data

        back_ref_info['href'] = self._generate_url(back_ref_type, back_ref_uuid)
        back_ref_info['uuid'] = back_ref_uuid

        result['%s_back_refs' % (back_ref_type)].append(back_ref_info)
    # end _read_back_ref


