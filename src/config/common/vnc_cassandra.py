#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import pycassa
from pycassa import ColumnFamily
from pycassa.batch import Mutator
from pycassa.system_manager import SystemManager, SIMPLE_STRATEGY
from pycassa.pool import AllServersUnavailable
import gevent

from vnc_api import vnc_api
from exceptions import NoIdError, DatabaseUnavailableError
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import API_SERVER_KEYSPACE_NAME, \
    CASSANDRA_DEFAULT_GC_GRACE_SECONDS
import time
from cfgm_common import jsonutils as json
import utils
import functools
import datetime
import re
from operator import itemgetter

class VncCassandraClient(object):
    # Name to ID mapping keyspace + tables
    _UUID_KEYSPACE_NAME = API_SERVER_KEYSPACE_NAME

    # TODO describe layout
    _OBJ_UUID_CF_NAME = 'obj_uuid_table'

    # TODO describe layout
    _OBJ_FQ_NAME_CF_NAME = 'obj_fq_name_table'

    # key: object type, column ($type:$id, uuid)
    # where type is entity object is being shared with. Project initially
    _OBJ_SHARED_CF_NAME = 'obj_shared_table'

    _MAX_COL = 10000000

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._UUID_KEYSPACE_NAME, [cls._OBJ_UUID_CF_NAME,
                                              cls._OBJ_FQ_NAME_CF_NAME,
                                              cls._OBJ_SHARED_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, server_list, db_prefix, keyspaces, logger,
                 generate_url=None, reset_config=[], credential=None):
        self._re_match_parent = re.compile('parent:')
        self._re_match_prop = re.compile('prop:')
        self._re_match_ref = re.compile('ref:')
        self._re_match_backref = re.compile('backref:')
        self._re_match_children = re.compile('children:')

        # bind CRUDL methods to all types
        for resource_type in vnc_api.all_resource_types:
            obj_type = resource_type.replace('-', '_')
            for oper in ('create', 'read', 'update', 'delete', 'list',
                         'count_children'):
                method = getattr(self, '_object_%s' %(oper))
                bound_method = functools.partial(method, resource_type)
                functools.update_wrapper(bound_method, method)
                setattr(self, '_cassandra_%s_%s' %(obj_type, oper),
                    bound_method)

        self._reset_config = reset_config
        self._cache_uuid_to_fq_name = {}
        if db_prefix:
            self._db_prefix = '%s_' %(db_prefix)
        else:
            self._db_prefix = ''
        self._server_list = server_list
        self._num_dbnodes = len(self._server_list)
        self._conn_state = ConnectionStatus.INIT
        self._logger = logger
        self._credential = credential

        # if no generate_url is specified, use a dummy function that always
        # returns an empty string
        self._generate_url = generate_url or (lambda x,y: '')
        self._cf_dict = {}
        self._keyspaces = {
            self._UUID_KEYSPACE_NAME: [(self._OBJ_UUID_CF_NAME, None),
                                       (self._OBJ_FQ_NAME_CF_NAME, None),
                                       (self._OBJ_SHARED_CF_NAME, None)]}

        if keyspaces:
            self._keyspaces.update(keyspaces)
        self._cassandra_init(server_list)
        self._cache_uuid_to_fq_name = {}
        self._obj_uuid_cf = self._cf_dict[self._OBJ_UUID_CF_NAME]
        self._obj_fq_name_cf = self._cf_dict[self._OBJ_FQ_NAME_CF_NAME]
        self._obj_shared_cf = self._cf_dict[self._OBJ_SHARED_CF_NAME]
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
            except AllServersUnavailable as e:
                if self._conn_state != ConnectionStatus.DOWN:
                    self._update_sandesh_status(ConnectionStatus.DOWN)
                    msg = 'Cassandra connection down. Exception in %s' \
                          %(str(func))
                    self._logger(msg, level=SandeshLevel.SYS_ERR)

                self._conn_state = ConnectionStatus.DOWN
                raise DatabaseUnavailableError(
                    'Error, AllServersUnavailable: %s'
                    %(utils.detailed_traceback()))

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

    def _cassandra_system_manager(self):
        # Retry till cassandra is up
        server_idx = 0
        connected = False
        while not connected:
            try:
                cass_server = self._server_list[server_idx]
                sys_mgr = SystemManager(cass_server, credentials=self._credential)
                connected = True
            except Exception:
                # TODO do only for
                # thrift.transport.TTransport.TTransportException
                server_idx = (server_idx + 1) % self._num_dbnodes
                time.sleep(3)
        return sys_mgr
    # end _cassandra_system_manager

    def _cassandra_ensure_keyspace(self, server_list,
                                   keyspace_name, cf_info_list):
        sys_mgr = self._cassandra_system_manager()

        if keyspace_name in self._reset_config:
            try:
                sys_mgr.drop_keyspace(keyspace_name)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                self._logger("Warning! " + str(e), level=SandeshLevel.SYS_WARN)

        try:
            sys_mgr.create_keyspace(keyspace_name, SIMPLE_STRATEGY,
                                    {'replication_factor': str(self._num_dbnodes)})
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
                max_retries=-1, timeout=5, credentials=self._credential)

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

    def _get_resource_class(self, obj_type):
        cls_name = '%s' %(utils.CamelCase(obj_type.replace('-', '_')))
        return getattr(vnc_api, cls_name)
    # end _get_resource_class

    def _object_create(self, res_type, obj_ids, obj_dict):
        obj_type = res_type.replace('-', '_')
        obj_class = self._get_resource_class(obj_type)

        # Gather column values for obj and updates to backrefs
        # in a batch and write it at the end
        bch = self._obj_uuid_cf.batch()

        obj_cols = {}
        obj_cols['fq_name'] = json.dumps(obj_dict['fq_name'])
        obj_cols['type'] = json.dumps(obj_type)
        if 'parent_type' in obj_dict:
            # non config-root child
            parent_type = obj_dict['parent_type']
            parent_method_type = parent_type.replace('-', '_')
            parent_fq_name = obj_dict['fq_name'][:-1]
            obj_cols['parent_type'] = json.dumps(parent_type)
            parent_uuid = self.fq_name_to_uuid(parent_method_type, parent_fq_name)
            self._create_child(bch, parent_method_type, parent_uuid, obj_type, obj_ids['uuid'])

        # Properties
        for prop_field in obj_class.prop_fields:
            field = obj_dict.get(prop_field)
            if field is None:
                continue
            if prop_field == 'id_perms':
                field['created'] = datetime.datetime.utcnow().isoformat()
                field['last_modified'] = field['created']

            self._create_prop(bch, obj_ids['uuid'], prop_field, field)

        # References
        # e.g. ref_field = 'network_ipam_refs'
        #      ref_type = 'network-ipam'
        #      ref_link_type = 'VnSubnetsType'
        #      is_weakref = False
        for ref_field in obj_class.ref_fields:
            ref_type, ref_link_type, _ = obj_class.ref_field_types[ref_field]
            refs = obj_dict.get(ref_field, [])
            for ref in refs:
                ref_uuid = self.fq_name_to_uuid(ref_type, ref['to'])
                ref_attr = ref.get('attr')
                ref_data = {'attr': ref_attr, 'is_weakref': False}
                self._create_ref(bch, obj_type, obj_ids['uuid'],
                    ref_type.replace('-', '_'), ref_uuid, ref_data)

        bch.insert(obj_ids['uuid'], obj_cols)
        bch.send()

        # Update fqname table
        fq_name_str = ':'.join(obj_dict['fq_name'])
        fq_name_cols = {utils.encode_string(fq_name_str) + ':' + obj_ids['uuid']: json.dumps(None)}
        self._obj_fq_name_cf.insert(obj_type, fq_name_cols)

        return (True, '')
    # end _object_create

    def _object_read(self, res_type, obj_uuids, field_names=None):
        # if field_names=None, all fields will be read/returned

        obj_type = res_type.replace('-', '_')
        obj_class = self._get_resource_class(obj_type)
        obj_uuid_cf = self._obj_uuid_cf

        # optimize for common case of reading non-backref, non-children fields
        # ignoring columns starting from 'b' and 'c' - significant performance
        # impact in scaled setting. e.g. read of project
        if (field_names is None or
            (set(field_names) & (obj_class.backref_fields | obj_class.children_fields))):
            # atleast one backref/children field is needed
            obj_rows = obj_uuid_cf.multiget(obj_uuids,
                                       column_count=self._MAX_COL,
                                       include_timestamp=True)
        else: # ignore reading backref + children columns
            obj_rows = obj_uuid_cf.multiget(obj_uuids,
                                       column_start='d',
                                       column_count=self._MAX_COL,
                                       include_timestamp=True)

        if (len(obj_uuids) == 1) and not obj_rows:
            raise NoIdError(obj_uuids[0])

        results = []
        for row_key in obj_rows:
            obj_uuid = row_key
            obj_cols = obj_rows[obj_uuid]
            result = {}
            result['uuid'] = obj_uuid
            result['fq_name'] = json.loads(obj_cols['fq_name'][0])
            for col_name in obj_cols.keys():
                if self._re_match_parent.match(col_name):
                    # non config-root child
                    (_, _, parent_uuid) = col_name.split(':')
                    parent_type = json.loads(obj_cols['parent_type'][0])
                    result['parent_type'] = parent_type
                    try:
                        result['parent_uuid'] = parent_uuid
                        result['parent_href'] = self._generate_url(parent_type, parent_uuid)
                    except NoIdError:
                        err_msg = 'Unknown uuid for parent ' + result['fq_name'][-2]
                        return (False, err_msg)

                if self._re_match_prop.match(col_name):
                    (_, prop_name) = col_name.split(':')
                    result[prop_name] = json.loads(obj_cols[col_name][0])

                if self._re_match_children.match(col_name):
                    (_, child_type, child_uuid) = col_name.split(':')
                    if field_names and '%ss' %(child_type) not in field_names:
                        continue

                    child_tstamp = obj_cols[col_name][1]
                    try:
                        self._read_child(result, obj_uuid, child_type, child_uuid, child_tstamp)
                    except NoIdError:
                        continue

                if self._re_match_ref.match(col_name):
                    (_, ref_type, ref_uuid) = col_name.split(':')
                    self._read_ref(result, obj_uuid, ref_type, ref_uuid, obj_cols[col_name][0])

                if self._re_match_backref.match(col_name):
                    (_, back_ref_type, back_ref_uuid) = col_name.split(':')
                    if field_names and '%s_back_refs' %(back_ref_type) not in field_names:
                        continue

                    try:
                        self._read_back_ref(result, obj_uuid, back_ref_type, back_ref_uuid,
                                            obj_cols[col_name][0])
                    except NoIdError:
                        continue

            # for all column names

            # sort children by creation time
            for child_field in obj_class.children_fields:
                if child_field not in result:
                    continue
                sorted_children = sorted(result[child_field],
                    key = itemgetter('tstamp'))
                # re-write result's children without timestamp
                result[child_field] = sorted_children
                [child.pop('tstamp') for child in result[child_field]]
            # for all children

            results.append(result)
        # end for all rows

        return (True, results)
    # end _object_read

    def _object_count_children(self, res_type, obj_uuid, child_type):
        if child_type is None:
            return (False, '')

        obj_type = res_type.replace('-', '_')
        obj_class = self._get_resource_class(obj_type)
        obj_uuid_cf = self._obj_uuid_cf
        if child_type not in obj_class.children_fields:
            return (False,
                '%s is not a child type of %s' %(child_type, obj_type))

        col_start = 'children:'+child_type[:-1]+':'
        col_finish = 'children:'+child_type[:-1]+';'
        num_children = obj_uuid_cf.get_count(obj_uuid,
                                   column_start=col_start,
                                   column_finish=col_finish,
                                   max_count=self._MAX_COL)
        return (True, num_children)
    # end _object_count_children

    def _object_update(self, res_type, obj_uuid, new_obj_dict):
        obj_type = res_type.replace('-', '_')
        obj_class = self._get_resource_class(obj_type)
         # Grab ref-uuids and properties in new version
        new_ref_infos = {}

        # Properties
        new_props = {}
        for prop_field in obj_class.prop_fields:
            if prop_field in new_obj_dict:
                new_props[prop_field] = new_obj_dict[prop_field]

        # References
        # e.g. ref_field = 'network_ipam_refs'
        #      ref_type = 'network-ipam'
        #      ref_link_type = 'VnSubnetsType'
        #      is_weakref = False
        for ref_field in obj_class.ref_fields:
            ref_type, ref_link_type, is_weakref = \
                obj_class.ref_field_types[ref_field]
            ref_obj_type = ref_type.replace('-', '_')

            if ref_field in new_obj_dict:
                new_refs = new_obj_dict[ref_field]
                new_ref_infos[ref_obj_type] = {}
                for new_ref in new_refs or []:
                    new_ref_uuid = self.fq_name_to_uuid(ref_type, new_ref['to'])
                    new_ref_attr = new_ref.get('attr')
                    new_ref_data = {'attr': new_ref_attr, 'is_weakref': is_weakref}
                    new_ref_infos[ref_obj_type][new_ref_uuid] = new_ref_data

        # Gather column values for obj and updates to backrefs
        # in a batch and write it at the end
        obj_uuid_cf = self._obj_uuid_cf
        obj_cols_iter = obj_uuid_cf.xget(obj_uuid)
        # TODO optimize this (converts tuple to dict)
        obj_cols = {}
        for col_info in obj_cols_iter:
            obj_cols[col_info[0]] = col_info[1]

        bch = obj_uuid_cf.batch()
        for col_name in obj_cols.keys():
            if re.match('prop:', col_name):
                (_, prop_name) = col_name.split(':')
                if prop_name == 'id_perms':
                    # id-perms always has to be updated for last-mod timestamp
                    # get it from request dict(or from db if not in request dict)
                    new_id_perms = new_obj_dict.get(prop_name, json.loads(obj_cols[col_name]))
                    self.update_last_modified(bch, obj_uuid, new_id_perms)
                elif prop_name in new_obj_dict:
                    self._update_prop(bch, obj_uuid, prop_name, new_props)

            if re.match('ref:', col_name):
                (_, ref_type, ref_uuid) = col_name.split(':')
                self._update_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid, new_ref_infos)
        # for all column names

        # create new refs
        for ref_type in new_ref_infos.keys():
            for ref_uuid in new_ref_infos[ref_type].keys():
                ref_data = new_ref_infos[ref_type][ref_uuid]
                self._create_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid, ref_data)

        # create new props
        for prop_name in new_props.keys():
            self._create_prop(bch, obj_uuid, prop_name, new_props[prop_name])

        bch.send()

        return (True, '')
    # end _object_update

    def _object_list(self, res_type, parent_uuids=None, back_ref_uuids=None,
                     obj_uuids=None, count=False, filters=None):
        obj_type = res_type.replace('-', '_')
        obj_class = self._get_resource_class(obj_type)

        children_fq_names_uuids = []
        if filters:
            fnames = filters.get('field_names', [])
            fvalues = filters.get('field_values', [])
            filter_fields = [(fnames[i], fvalues[i]) for i in range(len(fnames))]
        else:
            filter_fields = []

        def filter_rows(coll_infos, filter_cols, filter_params):
            filt_infos = {}
            coll_rows = obj_uuid_cf.multiget(coll_infos.keys(),
                                   columns=filter_cols,
                                   column_count=self._MAX_COL)
            for row in coll_rows:
                # give chance for zk heartbeat/ping
                gevent.sleep(0)
                full_match = True
                for fname, fval in filter_params:
                    if coll_rows[row]['prop:%s' %(fname)] != fval:
                        full_match = False
                        break
                if full_match:
                    filt_infos[row] = coll_infos[row]
            return filt_infos
        # end filter_rows

        def get_fq_name_uuid_list(obj_uuids):
            ret_list = []
            for obj_uuid in obj_uuids:
                try:
                    obj_fq_name = self.uuid_to_fq_name(obj_uuid)
                    ret_list.append((obj_fq_name, obj_uuid))
                except NoIdError:
                    pass
            return ret_list
        # end get_fq_name_uuid_list

        if parent_uuids:
            # go from parent to child
            obj_uuid_cf = self._obj_uuid_cf
            col_start = 'children:%s:' %(obj_type)
            col_fin = 'children:%s;' %(obj_type)
            try:
                obj_rows = obj_uuid_cf.multiget(parent_uuids,
                                       column_start=col_start,
                                       column_finish=col_fin,
                                       column_count=self._MAX_COL,
                                       include_timestamp=True)
            except pycassa.NotFoundException:
                if count:
                    return (True, 0)
                else:
                    return (True, children_fq_names_uuids)

            def filter_rows_parent_anchor(sort=False):
                # flatten to [('children:<type>:<uuid>', (<val>,<ts>), *]
                all_cols = [cols for obj_key in obj_rows.keys()
                                 for cols in obj_rows[obj_key].items()]
                all_child_infos = {}
                for col_name, col_val_ts in all_cols:
                    # give chance for zk heartbeat/ping
                    gevent.sleep(0)
                    child_uuid = col_name.split(':')[2]
                    if obj_uuids and child_uuid not in obj_uuids:
                        continue
                    all_child_infos[child_uuid] = {'uuid': child_uuid, 'tstamp': col_val_ts[1]}

                filter_cols = ['prop:%s' %(fname) for fname, _ in filter_fields]
                if filter_cols:
                    filt_child_infos = filter_rows(all_child_infos, filter_cols, filter_fields)
                else: # no filter specified
                    filt_child_infos = all_child_infos

                if not sort:
                    ret_child_infos = filt_child_infos.values()
                else:
                    ret_child_infos = sorted(filt_child_infos.values(), key=itemgetter('tstamp'))

                return get_fq_name_uuid_list(r['uuid'] for r in ret_child_infos)
            # end filter_rows_parent_anchor

            if count:
                return (True, len(filter_rows_parent_anchor()))

            children_fq_names_uuids = filter_rows_parent_anchor(sort=True)

        if back_ref_uuids:
            # go from anchor to backrefs
            obj_uuid_cf = self._obj_uuid_cf
            col_start = 'backref:%s:' %(obj_type)
            col_fin = 'backref:%s;' %(obj_type)
            try:
                obj_rows = obj_uuid_cf.multiget(back_ref_uuids,
                                       column_start=col_start,
                                       column_finish=col_fin,
                                       column_count=self._MAX_COL,
                                       include_timestamp=True)
            except pycassa.NotFoundException:
                if count:
                    return (True, 0)
                else:
                    return (True, children_fq_names_uuids)

            def filter_rows_backref_anchor():
                # flatten to [('backref:<obj-type>:<uuid>', (<val>,<ts>), *]
                all_cols = [cols for obj_key in obj_rows.keys()
                                 for cols in obj_rows[obj_key].items()]
                all_backref_infos = {}
                for col_name, col_val_ts in all_cols:
                    # give chance for zk heartbeat/ping
                    gevent.sleep(0)
                    backref_uuid = col_name.split(':')[2]
                    if obj_uuids and backref_uuid not in obj_uuids:
                        continue
                    all_backref_infos[backref_uuid] = \
                        {'uuid': backref_uuid, 'tstamp': col_val_ts[1]}

                filter_cols = ['prop:%s' %(fname) for fname, _ in filter_fields]
                if filter_cols:
                    filt_backref_infos = filter_rows(
                        all_backref_infos, filter_cols, filter_fields)
                else: # no filter specified
                    filt_backref_infos = all_backref_infos

                return get_fq_name_uuid_list(r['uuid'] for r in filt_backref_infos.values())
            # end filter_rows_backref_anchor

            if count:
                return (True, len(filter_rows_backref_anchor()))

            children_fq_names_uuids = filter_rows_backref_anchor()

        if not parent_uuids and not back_ref_uuids:
            obj_uuid_cf = self._obj_uuid_cf
            if obj_uuids:
                # exact objects specified
                def filter_rows_object_list():
                    all_obj_infos = {}
                    for obj_uuid in obj_uuids:
                        all_obj_infos[obj_uuid] = None

                    filter_cols = ['prop:%s' %(fname)
                                   for fname, _ in filter_fields]
                    if filter_cols:
                        filt_obj_infos = filter_rows(
                            all_obj_infos, filter_cols, filter_fields)
                    else: # no filters specified
                        filt_obj_infos = all_obj_infos

                    return get_fq_name_uuid_list(filt_obj_infos.keys())
                # end filter_rows_object_list

                if count:
                    return (True, len(filter_rows_object_list()))
                children_fq_names_uuids = filter_rows_object_list()

            else: # grab all resources of this type
                obj_fq_name_cf = self._obj_fq_name_cf
                try:
                    cols = obj_fq_name_cf.get('%s' %(obj_type),
                        column_count=self._MAX_COL)
                except pycassa.NotFoundException:
                    if count:
                        return (True, 0)
                    else:
                        return (True, children_fq_names_uuids)

                def filter_rows_no_anchor():
                    all_obj_infos = {}
                    for col_name, col_val in cols.items():
                        # give chance for zk heartbeat/ping
                        gevent.sleep(0)
                        col_name_arr = utils.decode_string(col_name).split(':')
                        obj_uuid = col_name_arr[-1]
                        all_obj_infos[obj_uuid] = (col_name_arr[:-1], obj_uuid)

                    filter_cols = ['prop:%s' %(fname) for fname, _ in filter_fields]
                    if filter_cols:
                        filt_obj_infos = filter_rows(all_obj_infos, filter_cols, filter_fields)
                    else: # no filters specified
                        filt_obj_infos = all_obj_infos

                    return filt_obj_infos.values()
                # end filter_rows_no_anchor

                if count:
                    return (True, len(filter_rows_no_anchor()))

                children_fq_names_uuids = filter_rows_no_anchor()

        return (True, children_fq_names_uuids)

    # end _object_list

    def _object_delete(self, res_type, obj_uuid):
        obj_type = res_type.replace('-', '_')
        obj_class = self._get_resource_class(obj_type)
        obj_uuid_cf = self._obj_uuid_cf
        fq_name = json.loads(
            obj_uuid_cf.get(obj_uuid, columns=['fq_name'])['fq_name'])
        bch = obj_uuid_cf.batch()

        # unlink from parent
        col_start = 'parent:'
        col_fin = 'parent;'
        col_name_iter = obj_uuid_cf.xget(
            obj_uuid, column_start=col_start, column_finish=col_fin)
        for (col_name, col_val) in col_name_iter:
            (_, parent_type, parent_uuid) = col_name.split(':')
            self._delete_child(
                bch, parent_type, parent_uuid, obj_type, obj_uuid)

        # remove refs
        col_start = 'ref:'
        col_fin = 'ref;'
        col_name_iter = obj_uuid_cf.xget(
            obj_uuid, column_start=col_start, column_finish=col_fin)
        for (col_name, col_val) in col_name_iter:
            (_, ref_type, ref_uuid) = col_name.split(':')
            self._delete_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid)

        bch.remove(obj_uuid)
        bch.send()

        # Update fqname table
        fq_name_str = ':'.join(fq_name)
        fq_name_col = utils.encode_string(fq_name_str) + ':' + obj_uuid
        self._obj_fq_name_cf.remove(obj_type, columns = [fq_name_col])

        return (True, '')
    # end _object_delete

    def read(self, method_name, *args, **kwargs):
        method = getattr(self, '_cassandra_%s_read' % (method_name))
        return method(*args, **kwargs)
    # end read

    def count_children(self, method_name, *args, **kwargs):
        method = getattr(self, '_cassandra_%s_count_children' % (method_name))
        return method(*args, **kwargs)
    # end count_children

    def list(self, method_name, *args, **kwargs):
        method = getattr(self, '_cassandra_%s_list' % (method_name))
        return method(*args, **kwargs)
    # end list

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

    # return all objects shared with a (group, id)
    def get_shared(self, obj_type, id, group = 'tenant'):
        result = []
        method_name = obj_type.replace('-', '_')
        col_start = '%s:%s:' % (group, id)
        col_fin = '%s:%s;' % (group, id)
        try:
            col_info_iter = self._obj_shared_cf.xget(
                method_name, column_start=col_start, column_finish=col_fin)
        except pycassa.NotFoundException:
            return None

        col_infos = list(col_info_iter)

        if len(col_infos) == 0:
            return None

        for (col_name, col_val) in col_infos:
            # ('*:*:f7963198-08a4-4b96-a02e-41cc66593163', u'7')
            obj_uuid = col_name.split(':')[-1]
            result.append((obj_uuid, int(col_val)))

        return result

    # share 'uuid' object with <group:id>
    def set_shared(self, obj_type, uuid, id, group = 'tenant', rwx = 7):
        col_infos = []
        #col_name = (group, id, uuid)
        col_name = '%s:%s:%s' % (group, id, uuid)
        method_name = obj_type.replace('-', '_')
        try:
            col_info_json = self._obj_shared_cf.get(
                    method_name, columns=[col_name])
        except pycassa.NotFoundException:
            # not found ... add it
            self._obj_shared_cf.insert(method_name, {col_name : str(rwx)})

    # delete share of 'uuid' object with <group:id>
    def del_shared(self, obj_type, uuid, id, group = 'tenant'):
        col_name = '%s:%s:%s' % (group, id, uuid)
        method_name = obj_type.replace('-', '_')
        self._obj_shared_cf.remove(method_name, columns=[col_name])

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


