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

import gevent
from pprint import pformat

from vnc_api import vnc_api
from .exceptions import NoIdError, VncError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns import constants as vns_constants
from cfgm_common import jsonutils as json
from . import utils
import datetime
from operator import itemgetter
from collections import OrderedDict
from cfgm_common.cassandra_driver_thrift import CassandraDriverThrift


class VncCassandraClient(object):

    # Name to ID mapping keyspace + tables
    _UUID_KEYSPACE_NAME = vns_constants.API_SERVER_KEYSPACE_NAME

    # TODO describe layout
    _OBJ_UUID_CF_NAME = 'obj_uuid_table'

    # TODO describe layout
    _OBJ_FQ_NAME_CF_NAME = 'obj_fq_name_table'

    # key: object type, column ($type:$id, uuid)
    # where type is entity object is being shared with. Project initially
    _OBJ_SHARED_CF_NAME = 'obj_shared_table'

    _UUID_KEYSPACE = {
        _UUID_KEYSPACE_NAME: {
            _OBJ_UUID_CF_NAME: {
                'cf_args': {
                    'autopack_names': False,
                    'autopack_values': False,
                },
            },
            _OBJ_FQ_NAME_CF_NAME: {
                'cf_args': {
                    'autopack_values': False,
                },
            },
            _OBJ_SHARED_CF_NAME: {}
        }
    }

    @staticmethod
    def _is_metadata(column_name):
        return column_name[:5] == 'META:'

    @staticmethod
    def _is_parent(column_name):
        return column_name[:7] == 'parent:'

    @staticmethod
    def _is_prop(column_name):
        return column_name[:5] == 'prop:'

    @staticmethod
    def _is_prop_list(column_name):
        return column_name[:6] == 'propl:'

    @staticmethod
    def _is_prop_map(column_name):
        return column_name[:6] == 'propm:'

    @staticmethod
    def _is_ref(column_name):
        return column_name[:4] == 'ref:'

    @staticmethod
    def _is_backref(column_name):
        return column_name[:8] == 'backref:'

    @staticmethod
    def _is_children(column_name):
        return column_name[:9] == 'children:'

    def add(self, cf_name, key, value):
        try:
            self._cassandra_driver.get_cf(cf_name).insert(key, value)
            return True
        except:
            return False

    def delete(self, cf_name, key, columns=None):
        try:
            self._cassandra_driver.get_cf(cf_name).remove(key, columns=columns)
            return True
        except:
            return False

    def _get_resource_class(self, obj_type):
        if hasattr(self, '_db_client_mgr'):
            return self._db_client_mgr.get_resource_class(obj_type)

        cls_name = '%s' % (utils.CamelCase(obj_type))
        return getattr(vnc_api, cls_name)
    # end _get_resource_class

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._UUID_KEYSPACE_NAME, [cls._OBJ_UUID_CF_NAME,
                                              cls._OBJ_FQ_NAME_CF_NAME,
                                              cls._OBJ_SHARED_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, server_list, db_prefix, rw_keyspaces, ro_keyspaces,
                 logger, generate_url=None, reset_config=False,
                 credential=None, walk=True, obj_cache_entries=0,
                 obj_cache_exclude_types=None, debug_obj_cache_types=None,
                 log_response_time=None, ssl_enabled=False, ca_certs=None,
                 pool_size=0):

        # TODO: Instantiate this driver based on if it's python2.7 (Thrift)
        #       python 3 (CQL)
        self._cassandra_driver = CassandraDriverThrift(
                 server_list, db_prefix, rw_keyspaces, ro_keyspaces,
                 logger, generate_url, reset_config,
                 credential, walk, obj_cache_entries,
                 obj_cache_exclude_types, debug_obj_cache_types,
                 log_response_time, ssl_enabled, ca_certs,
                 pool_size)

        self._logger = self._cassandra_driver._logger
        self._cache_uuid_to_fq_name = {}
        self._obj_uuid_cf = (self._cassandra_driver.
                                _cf_dict[self._OBJ_UUID_CF_NAME])
        self._obj_fq_name_cf = (self._cassandra_driver.
                                _cf_dict[self._OBJ_FQ_NAME_CF_NAME])
        self._obj_shared_cf = (self._cassandra_driver.
                                _cf_dict[self._OBJ_SHARED_CF_NAME])

        self._obj_cache_mgr = ObjectCacheManager(
            logger,
            self,
            max_entries=obj_cache_entries,
            obj_cache_exclude_types=obj_cache_exclude_types,
            debug_obj_cache_types=debug_obj_cache_types,
        )
        self._obj_cache_exclude_types = obj_cache_exclude_types or []

        # these functions make calls to pycassa xget() and get_range()
        # generator functions which can't be wrapped around handle_exceptions()
        # at the time of cassandra init, hence need to wrap these functions that
        # uses it to catch cassandra connection failures.
        self.object_update = self._cassandra_driver._handle_exceptions(
            self.object_update)
        self.object_list = self._cassandra_driver._handle_exceptions(
            self.object_list)
        self.object_read = self._cassandra_driver._handle_exceptions(
            self.object_read)
        self.object_raw_read = self._cassandra_driver._handle_exceptions(
            self.object_raw_read)
        self.object_delete = self._cassandra_driver._handle_exceptions(
            self.object_delete)
        self.prop_collection_read = self._cassandra_driver._handle_exceptions(
            self.prop_collection_read)
        self.uuid_to_fq_name = self._cassandra_driver._handle_exceptions(
            self.uuid_to_fq_name)
        self.uuid_to_obj_type = self._cassandra_driver._handle_exceptions(
            self.uuid_to_obj_type)
        self.fq_name_to_uuid = self._cassandra_driver._handle_exceptions(
            self.fq_name_to_uuid)
        self.get_shared = self._cassandra_driver._handle_exceptions(
            self.get_shared)
        self.walk = self._cassandra_driver._handle_exceptions(self.walk)

        if walk:
            self.walk()

    # end __init__

    def _create_prop(self, bch, obj_uuid, prop_name, prop_val):
        self._cassandra_driver.insert(
            obj_uuid,
            {'prop:%s' % (prop_name): json.dumps(prop_val)},
            batch=bch)
    # end _create_prop

    def _update_prop(self, bch, obj_uuid, prop_name, new_props):
        if new_props[prop_name] is None:
            self._cassandra_driver.remove(obj_uuid,
                                          columns=['prop:' + prop_name],
                                          batch=bch)
        else:
            self._cassandra_driver.insert(
                obj_uuid,
                {'prop:' + prop_name: json.dumps(new_props[prop_name])},
                batch=bch)

        # prop has been accounted for, remove so only new ones remain
        del new_props[prop_name]
    # end _update_prop

    def _add_to_prop_list(self, bch, obj_uuid, prop_name,
                          prop_elem_value, prop_elem_position):
        self._cassandra_driver.insert(obj_uuid,
                   {'propl:%s:%s' % (prop_name, prop_elem_position):
                    json.dumps(prop_elem_value)},
                   batch=bch)
    # end _add_to_prop_list

    def _delete_from_prop_list(self, bch, obj_uuid, prop_name,
                               prop_elem_position):
        self._cassandra_driver.remove(
                   obj_uuid,
                   columns=['propl:%s:%s' % (prop_name, prop_elem_position)],
                   batch=bch)
    # end _delete_from_prop_list

    def _set_in_prop_map(self, bch, obj_uuid, prop_name,
                         prop_elem_value, prop_elem_position):
        self._cassandra_driver.insert(obj_uuid,
                   {'propm:%s:%s' % (prop_name, prop_elem_position):
                    json.dumps(prop_elem_value)},
                   batch=bch)
    # end _set_in_prop_map

    def _delete_from_prop_map(self, bch, obj_uuid, prop_name,
                              prop_elem_position):
        self._cassandra_driver.remove(
                   obj_uuid,
                   columns=['propm:%s:%s' % (prop_name, prop_elem_position)],
                   batch=bch)
    # end _delete_from_prop_map

    def _create_child(self, bch, parent_type, parent_uuid,
                      child_type, child_uuid):
        child_col = {'children:%s:%s' %
                     (child_type, child_uuid): json.dumps(None)}
        self._cassandra_driver.insert(parent_uuid, child_col, batch=bch)

        parent_col = {'parent:%s:%s' %
                      (parent_type, parent_uuid): json.dumps(None)}
        self._cassandra_driver.insert(child_uuid, parent_col, batch=bch)

        # update latest_col_ts on parent object
        if parent_type not in self._obj_cache_exclude_types:
            self.update_latest_col_ts(bch, parent_uuid)
    # end _create_child

    def _delete_child(self, bch, parent_type, parent_uuid,
                      child_type, child_uuid):
        self._cassandra_driver.remove(
                    parent_uuid,
                    columns=['children:%s:%s' % (child_type, child_uuid)],
                    batch=bch)

        # update latest_col_ts on parent object
        if parent_type not in self._obj_cache_exclude_types:
            self.update_latest_col_ts(bch, parent_uuid)
    # end _delete_child

    def _create_ref(self, bch, obj_type, obj_uuid, ref_obj_type, ref_uuid,
                    ref_data):
        symmetric_ref_updates = []
        self._cassandra_driver.insert(
             obj_uuid, {'ref:%s:%s' %
             (ref_obj_type, ref_uuid): json.dumps(ref_data)},
             batch=bch)
        if obj_type == ref_obj_type:
            self._cassandra_driver.insert(
                ref_uuid, {'ref:%s:%s' %
                (obj_type, obj_uuid): json.dumps(ref_data)},
                batch=bch)
            self.update_last_modified(bch, obj_type, ref_uuid)
            symmetric_ref_updates = [ref_uuid]
        else:
            self._cassandra_driver.insert(
                ref_uuid, {'backref:%s:%s' %
                (obj_type, obj_uuid): json.dumps(ref_data)},
                batch=bch)
        # update latest_col_ts on referred object
        if ref_obj_type not in self._obj_cache_exclude_types:
            if ref_obj_type == obj_type:
                # evict other side of ref since it is stale from
                # GET /<old-ref-uuid> pov.
                self._obj_cache_mgr.evict(obj_type, [ref_uuid])
            else:
                self.update_latest_col_ts(bch, ref_uuid)
        return symmetric_ref_updates
    # end _create_ref

    def _update_ref(self, bch, obj_type, obj_uuid, ref_obj_type, old_ref_uuid,
                    new_ref_infos):
        if ref_obj_type not in new_ref_infos:
            # update body didn't touch this type, nop
            return []

        symmetric_ref_updates = []
        if old_ref_uuid not in new_ref_infos[ref_obj_type]:
            # remove old ref
            self._cassandra_driver.remove(
                    obj_uuid,
                    columns=['ref:%s:%s' % (ref_obj_type, old_ref_uuid)],
                    batch=bch)
            if obj_type == ref_obj_type:
                self._cassandra_driver.remove(
                        old_ref_uuid,
                        columns=['ref:%s:%s' % (obj_type, obj_uuid)],
                        batch=bch)
                try:
                    self.update_last_modified(bch, obj_type, old_ref_uuid)
                    symmetric_ref_updates = [old_ref_uuid]
                except NoIdError as e:
                    # old_ref_uuid might have been deleted
                    # if cache has the link, it will be evicted
                    # if cache doesn't have, keyerror is caught and continued
                    pass
            else:
                self._cassandra_driver.remove(
                        old_ref_uuid,
                        columns=['backref:%s:%s' % (obj_type, obj_uuid)],
                        batch=bch)
        else:
            # retain old ref with new ref attr
            new_ref_data = new_ref_infos[ref_obj_type][old_ref_uuid]
            self._cassandra_driver.insert(
                   obj_uuid,
                   {'ref:%s:%s' % (ref_obj_type, old_ref_uuid):
                   json.dumps(new_ref_data)},
                   batch=bch)
            if obj_type == ref_obj_type:
                self._cassandra_driver.insert(
                    old_ref_uuid,
                    {'ref:%s:%s' % (obj_type, obj_uuid):
                    json.dumps(new_ref_data)},
                    batch=bch)
                self.update_last_modified(bch, obj_type, old_ref_uuid)
                symmetric_ref_updates = [old_ref_uuid]
            else:
                self._cassandra_driver.insert(
                    old_ref_uuid,
                    {'backref:%s:%s' % (obj_type, obj_uuid):
                    json.dumps(new_ref_data)},
                    batch=bch)
            # uuid has been accounted for, remove so only new ones remain
            del new_ref_infos[ref_obj_type][old_ref_uuid]

        # update latest_col_ts on referred object
        if ref_obj_type not in self._obj_cache_exclude_types:
            if ref_obj_type == obj_type:
                # evict other side of ref since it is stale from
                # GET /<old-ref-uuid> pov.
                self._obj_cache_mgr.evict(obj_type, [old_ref_uuid])
            else:
                self.update_latest_col_ts(bch, old_ref_uuid)
        return symmetric_ref_updates
    # end _update_ref

    def _delete_ref(self, bch, obj_type, obj_uuid, ref_obj_type, ref_uuid):
        send = False
        symmetric_ref_updates = []
        if bch is None:
            send = True
            bch = self._obj_uuid_cf.batch()
        self._cassandra_driver.remove(
                obj_uuid,
                columns=['ref:%s:%s' % (ref_obj_type, ref_uuid)],
                batch=bch)
        if obj_type == ref_obj_type:
            self._cassandra_driver.remove(ref_uuid, columns=[
                                          'ref:%s:%s' % (obj_type, obj_uuid)],
                                          batch=bch)
            try:
                self.update_last_modified(bch, obj_type, ref_uuid)
                symmetric_ref_updates = [ref_uuid]
            except NoIdError as e:
                # ref_uuid might have been deleted
                # if cache has the link, it will be evicted
                # if cache doesn't have, keyerror is caught and continued
                pass
        else:
            self._cassandra_driver.remove(
                    ref_uuid,
                    columns=['backref:%s:%s' % (obj_type, obj_uuid)],
                    batch=bch)

        # update latest_col_ts on referred object
        if ref_obj_type not in self._obj_cache_exclude_types:
            if ref_obj_type == obj_type:
                # evict other side of ref since it is stale from
                # GET /<old-ref-uuid> pov.
                self._obj_cache_mgr.evict(obj_type, [ref_uuid])
            else:
                self.update_latest_col_ts(bch, ref_uuid)

        if send:
            bch.send()
        return symmetric_ref_updates

    # end _delete_ref
    def _get_xsd_class(self, xsd_type):
        return getattr(vnc_api, xsd_type)
    # end _get_xsd_class

    def object_create(self, obj_type, obj_id, obj_dict,
                      uuid_batch=None, fqname_batch=None):
        obj_class = self._get_resource_class(obj_type)

        if uuid_batch:
            bch = uuid_batch
        else:
            # Gather column values for obj and updates to backrefs
            # in a batch and write it at the end
            bch = self._obj_uuid_cf.batch()

        obj_cols = {}
        obj_cols['fq_name'] = json.dumps(obj_dict['fq_name'])
        obj_cols['type'] = json.dumps(obj_type)
        if obj_type not in self._obj_cache_exclude_types:
            obj_cols['META:latest_col_ts'] = json.dumps(None)
        if 'parent_type' in obj_dict:
            # non config-root child
            parent_type = obj_dict['parent_type']
            if parent_type not in obj_class.parent_types:
                msg = ("Invalid parent type: %s not in %s" %
                       (parent_type, obj_class.parent_types))
                return False, (400, msg)
            parent_object_type = self._get_resource_class(
                parent_type).object_type
            parent_fq_name = obj_dict['fq_name'][:-1]
            obj_cols['parent_type'] = json.dumps(parent_type)
            parent_uuid = self.fq_name_to_uuid(parent_object_type,
                                               parent_fq_name)
            self._create_child(bch, parent_object_type, parent_uuid, obj_type,
                               obj_id)

        # Properties
        for prop_field in obj_class.prop_fields:
            field = obj_dict.get(prop_field)
            # Specifically checking for None
            if field is None:
                continue
            if prop_field == 'id_perms':
                field['created'] = datetime.datetime.utcnow().isoformat()
                field['last_modified'] = field['created']

            if prop_field in obj_class.prop_list_fields:
                # store list elements in list order
                # iterate on wrapped element or directly or prop field
                if obj_class.prop_list_field_has_wrappers[prop_field]:
                    wrapper_field_keys = list(field.keys())
                    if wrapper_field_keys:
                        wrapper_field = wrapper_field_keys[0]
                        list_coll = field[wrapper_field]
                    else:
                        list_coll = []
                else:
                    list_coll = field

                for i in range(len(list_coll)):
                    self._add_to_prop_list(
                        bch, obj_id, prop_field, list_coll[i], str(i))
            elif prop_field in obj_class.prop_map_fields:
                # iterate on wrapped element or directly or prop field
                if obj_class.prop_map_field_has_wrappers[prop_field]:
                    wrapper_field_keys = list(field.keys())
                    if wrapper_field_keys:
                        wrapper_field = wrapper_field_keys[0]
                        map_coll = field[wrapper_field]
                    else:
                        map_coll = []
                else:
                    map_coll = field

                map_key_name = obj_class.prop_map_field_key_names[prop_field]
                for map_elem in map_coll:
                    map_key = map_elem[map_key_name]
                    self._set_in_prop_map(
                        bch, obj_id, prop_field, map_elem, map_key)
            else:
                self._create_prop(bch, obj_id, prop_field, field)

        # References
        # e.g. ref_field = 'network_ipam_refs'
        #      ref_res_type = 'network-ipam'
        #      ref_link_type = 'VnSubnetsType'
        #      is_weakref = False
        symmetric_ref_updates = []
        for ref_field in obj_class.ref_fields:
            ref_fld_types_list = list(obj_class.ref_field_types[ref_field])
            ref_res_type = ref_fld_types_list[0]
            ref_link_type = ref_fld_types_list[1]
            ref_obj_type = self._get_resource_class(ref_res_type).object_type
            refs = obj_dict.get(ref_field, [])
            for ref in refs:
                ref_uuid = self.fq_name_to_uuid(ref_obj_type, ref['to'])
                ref_attr = ref.get('attr')
                ref_data = {'attr': ref_attr, 'is_weakref': False}
                ret = self._create_ref(bch, obj_type, obj_id, ref_obj_type, ref_uuid,
                                       ref_data)
                symmetric_ref_updates.extend(ret)

        self._cassandra_driver.insert(obj_id, obj_cols, batch=bch)
        if not uuid_batch:
            bch.send()

        # Update fqname table
        fq_name_str = ':'.join(obj_dict['fq_name'])
        fq_name_cols = {utils.encode_string(fq_name_str) + ':' + obj_id:
                        json.dumps(None)}
        if fqname_batch:
            fqname_batch.insert(obj_type, fq_name_cols)
        else:
            self._obj_fq_name_cf.insert(obj_type, fq_name_cols)

        return (True, symmetric_ref_updates)
    # end object_create

    def object_raw_read(self, obj_type, obj_uuids, prop_names):
        obj_class = self._get_resource_class(obj_type)
        hit_obj_dicts, miss_uuids = self._obj_cache_mgr.read(
            obj_class, obj_uuids, prop_names, False)
        miss_obj_rows = self._cassandra_driver.multiget(
            self._OBJ_UUID_CF_NAME, miss_uuids,
            ['prop:' + x for x in prop_names])

        miss_obj_dicts = []
        for obj_uuid, columns in list(miss_obj_rows.items()):
            miss_obj_dict = {'uuid': obj_uuid}
            for prop_name in columns:
                # strip 'prop:' before sending result back
                miss_obj_dict[prop_name[5:]] = columns[prop_name]
            miss_obj_dicts.append(miss_obj_dict)

        return hit_obj_dicts + miss_obj_dicts

    def object_read(self, obj_type, obj_uuids, field_names=None,
                    ret_readonly=False):
        if not obj_uuids:
            return (True, [])
        # if field_names=None, all fields will be read/returned
        req_fields = field_names
        obj_class = self._get_resource_class(obj_type)
        ref_fields = obj_class.ref_fields
        backref_fields = obj_class.backref_fields
        children_fields = obj_class.children_fields
        list_fields = obj_class.prop_list_fields
        map_fields = obj_class.prop_map_fields
        prop_fields = obj_class.prop_fields - (list_fields | map_fields)
        if ((ret_readonly is False) or
            (obj_type in self._obj_cache_exclude_types)):
            ignore_cache = True
        else:
            ignore_cache = False

        # optimize for common case of reading non-backref, non-children fields
        # ignoring columns starting from 'b' and 'c' - significant performance
        # impact in scaled setting. e.g. read of project
        # For caching (when ret values will be used for readonly
        # e.g. object read/list context):
        #   1. pick the hits, and for the misses..
        #   2. read from db, cache, filter with fields
        #      else read from db with specified field filters
        if (field_names is None or
            set(field_names) & (backref_fields | children_fields)):
            # atleast one backref/children field is needed
            include_backrefs_children = True
            if ignore_cache:
                hit_obj_dicts = []
                miss_uuids = obj_uuids
            else:
                hit_obj_dicts, miss_uuids = self._obj_cache_mgr.read(
                    obj_class,
                    obj_uuids,
                    field_names,
                    include_backrefs_children,
                )
            miss_obj_rows = self._cassandra_driver.multiget(
                self._OBJ_UUID_CF_NAME, miss_uuids,
                timestamp=True)
        else:
            # ignore reading backref + children columns
            include_backrefs_children = False
            if ignore_cache:
                hit_obj_dicts = []
                miss_uuids = obj_uuids
            else:
                hit_obj_dicts, miss_uuids = self._obj_cache_mgr.read(
                    obj_class,
                    obj_uuids,
                    field_names,
                    include_backrefs_children,
                )
            miss_obj_rows = self._cassandra_driver.multiget(
                                          self._OBJ_UUID_CF_NAME,
                                          miss_uuids,
                                          start='d',
                                          timestamp=True)

        if (ignore_cache or
                self._obj_cache_mgr.max_entries < len(miss_uuids)):
            # caller may modify returned value, or
            # cannot fit in cache,
            # just render with filter and don't cache
            rendered_objs = self._render_obj_from_db(
                obj_class, miss_obj_rows, req_fields,
                include_backrefs_children)
            obj_dicts = hit_obj_dicts + \
                [v['obj_dict'] for k,v in list(rendered_objs.items())]
        else:
            # can fit and caller won't modify returned value,
            # so render without filter, cache and return
            # cached value
            rendered_objs_to_cache = self._render_obj_from_db(
                obj_class, miss_obj_rows, None,
                include_backrefs_children)
            field_filtered_objs = self._obj_cache_mgr.set(
                obj_type,
                rendered_objs_to_cache,
                req_fields,
                include_backrefs_children,
            )
            obj_dicts = hit_obj_dicts + field_filtered_objs

        if not obj_dicts:
            if len(obj_uuids) == 1:
                raise NoIdError(obj_uuids[0])
            else:
                return (True, [])

        return (True, obj_dicts)
    # end object_read

    def object_count_children(self, obj_type, obj_uuid, child_type):
        if child_type is None:
            return (False, '')

        obj_class = self._get_resource_class(obj_type)
        obj_uuid_cf = self._obj_uuid_cf
        if child_type not in obj_class.children_fields:
            return (False,
                    '%s is not a child type of %s' % (child_type, obj_type))

        col_start = 'children:' + child_type[:-1] + ':'
        col_finish = 'children:' + child_type[:-1] + ';'
        num_children = obj_uuid_cf.get_count(obj_uuid,
                                             column_start=col_start,
                                             column_finish=col_finish)
        return (True, num_children)
    # end object_count_children

    def update_last_modified(self, bch, obj_type, obj_uuid, id_perms=None):
        if id_perms is None:
            id_perms = self._cassandra_driver.get_one_col(
                                       self._OBJ_UUID_CF_NAME,
                                       obj_uuid,
                                       'prop:id_perms')
        id_perms['last_modified'] = datetime.datetime.utcnow().isoformat()
        self._update_prop(bch, obj_uuid, 'id_perms', {'id_perms': id_perms})
        if obj_type not in self._obj_cache_exclude_types:
            self.update_latest_col_ts(bch, obj_uuid)
    # end update_last_modified

    def update_latest_col_ts(self, bch, obj_uuid):
        try:
            self._cassandra_driver.get_one_col(self._OBJ_UUID_CF_NAME,
                                               obj_uuid,
                                               'type')
        except NoIdError:
            return

        self._cassandra_driver.insert(obj_uuid,
                                      {'META:latest_col_ts':
                                       json.dumps(None)},
                                      batch=bch)
    # end update_latest_col_ts

    def object_update(self, obj_type, obj_uuid, new_obj_dict, uuid_batch=None):
        obj_class = self._get_resource_class(obj_type)
        # Grab ref-uuids and properties in new version
        new_ref_infos = {}
        symmetric_ref_updates = []

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
            ref_fld_types_list = list(obj_class.ref_field_types[ref_field])
            ref_res_type = ref_fld_types_list[0]
            ref_link_type = ref_fld_types_list[1]
            is_weakref = ref_fld_types_list[2]
            ref_obj_type = self._get_resource_class(ref_res_type).object_type

            if ref_field in new_obj_dict:
                new_refs = new_obj_dict[ref_field]
                new_ref_infos[ref_obj_type] = {}
                for new_ref in new_refs or []:
                    try:
                        new_ref_uuid = new_ref['uuid']
                    except KeyError:
                        new_ref_uuid = self.fq_name_to_uuid(ref_obj_type,
                                                            new_ref['to'])
                    new_ref_attr = new_ref.get('attr')
                    new_ref_data = {'attr': new_ref_attr,
                                    'is_weakref': is_weakref}
                    new_ref_infos[ref_obj_type][new_ref_uuid] = new_ref_data

        # Gather column values for obj and updates to backrefs
        # in a batch and write it at the end
        obj_uuid_cf = self._obj_uuid_cf

        if uuid_batch:
            bch = uuid_batch
        else:
            bch = obj_uuid_cf.batch()

        for col_name, col_value in obj_uuid_cf.xget(obj_uuid):
            if self._is_prop(col_name):
                (_, prop_name) = col_name.split(':')
                if prop_name == 'id_perms':
                    # id-perms always has to be updated for last-mod timestamp
                    # get it from request dict(or from db if not in request dict)
                    new_id_perms = new_obj_dict.get(
                        prop_name, json.loads(col_value))
                    self.update_last_modified(
                        bch, obj_type, obj_uuid, new_id_perms)
                elif prop_name in new_obj_dict:
                    self._update_prop(
                        bch, obj_uuid, prop_name, new_props)

            if self._is_prop_list(col_name):
                (_, prop_name, prop_elem_position) = col_name.split(':', 2)
                if prop_name in new_props:
                    # delete all old values of prop list
                    self._delete_from_prop_list(
                        bch, obj_uuid, prop_name, prop_elem_position)

            if self._is_prop_map(col_name):
                (_, prop_name, prop_elem_position) = col_name.split(':', 2)
                if prop_name in new_props:
                    # delete all old values of prop list
                    self._delete_from_prop_map(
                        bch, obj_uuid, prop_name, prop_elem_position)

            if self._is_ref(col_name):
                (_, ref_type, ref_uuid) = col_name.split(':')
                ret = self._update_ref(bch, obj_type, obj_uuid, ref_type,
                                       ref_uuid, new_ref_infos)
                symmetric_ref_updates.extend(ret)
        # for all column names

        # create new refs
        for ref_type in list(new_ref_infos.keys()):
            for ref_uuid in list(new_ref_infos[ref_type].keys()):
                ref_data = new_ref_infos[ref_type][ref_uuid]
                ret = self._create_ref(bch, obj_type, obj_uuid, ref_type,
                                       ref_uuid, ref_data)
                symmetric_ref_updates.extend(ret)

        # create new props
        for prop_name in list(new_props.keys()):
            if prop_name in obj_class.prop_list_fields:
                # store list elements in list order
                # iterate on wrapped element or directly on prop field
                # for wrapped lists, store without the wrapper. regenerate
                # wrapper on read
                if (obj_class.prop_list_field_has_wrappers[prop_name] and
                        new_props[prop_name]):
                    wrapper_field = list(new_props[prop_name].keys())[0]
                    list_coll = new_props[prop_name][wrapper_field]
                else:
                    list_coll = new_props[prop_name]

                for i in range(len(list_coll)):
                    self._add_to_prop_list(bch, obj_uuid, prop_name,
                                           list_coll[i], str(i))
            elif prop_name in obj_class.prop_map_fields:
                # store map elements in key order
                # iterate on wrapped element or directly on prop field
                # for wrapped lists, store without the wrapper. regenerate
                # wrapper on read
                if (obj_class.prop_map_field_has_wrappers[prop_name] and
                        new_props[prop_name]):
                    wrapper_field = list(new_props[prop_name].keys())[0]
                    map_coll = new_props[prop_name][wrapper_field]
                else:
                    map_coll = new_props[prop_name]

                map_key_name = obj_class.prop_map_field_key_names[prop_name]
                for map_elem in map_coll:
                    map_key = map_elem[map_key_name]
                    self._set_in_prop_map(bch, obj_uuid, prop_name,
                                          map_elem, map_key)
            else:
                self._create_prop(bch, obj_uuid, prop_name, new_props[prop_name])

        if not uuid_batch:
            try:
                bch.send()
            finally:
                self._obj_cache_mgr.evict(obj_type, [obj_uuid])

        return (True, symmetric_ref_updates)
    # end object_update

    def object_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                     obj_uuids=None, count=False, filters=None, tag_filters=None,
                     paginate_start=None, paginate_count=None):

        obj_class = self._get_resource_class(obj_type)
        children_fq_names_uuids = []
        ret_marker = None
        anchored_op = True

        def filter_rows(coll_infos, filters=None, tag_filters=None):
            if not coll_infos or not filters:
                return coll_infos

            if not tag_filters:
                tag_filters = []

            print('******************************************************')
            print('tag_filters', tag_filters)
            print('******************************************************')

            filtered_infos = {}
            columns = ['prop:%s' % filter_key for filter_key in filters if
                       filter_key in obj_class.prop_fields] + tag_filters
            if not columns:
                return coll_infos
            rows = self._cassandra_driver.multiget(self._OBJ_UUID_CF_NAME,
                                                   list(coll_infos.keys()),
                                                   columns=columns)
            for obj_uuid, properties in list(rows.items()):
                # give chance for zk heartbeat/ping
                gevent.sleep(0)

                full_match = True
                for filter_key, filter_values in list(filters.items()):
                    property = 'prop:%s' % filter_key
                    if property not in properties:
                        full_match = False
                        break
                    prop_value = properties[property]
                    if isinstance(prop_value, dict):
                        for filter_value in filter_values:
                            try:
                                filter_dict = json.loads(filter_value)
                            except ValueError:
                                continue
                            if (list(filter_dict.items()) <=
                                    list(prop_value.viewitems())):
                                break
                        else:
                            full_match = False
                            break
                    elif prop_value not in filter_values:
                        full_match = False
                        break

                if full_match:
                    filtered_infos[obj_uuid] = coll_infos[obj_uuid]
            return filtered_infos
        # end filter_rows

        def get_fq_name_uuid_list(obj_uuids):
            ret_list = []
            for obj_uuid in obj_uuids:
                try:
                    if obj_type != self.uuid_to_obj_type(obj_uuid):
                        continue
                    obj_fq_name = self.uuid_to_fq_name(obj_uuid)
                    ret_list.append((obj_fq_name, obj_uuid))
                except NoIdError:
                    pass
            return ret_list
        # end get_fq_name_uuid_list

        if parent_uuids:
            # go from parent to child
            ## tune start and count if paginated on same row
            #if paginate_start and (len(parent_uuids) == 1):
            if paginate_start and paginate_start != '0':
                start = 'children:%s:%s' % (obj_type,
                    paginate_start[:-1]+chr(ord(paginate_start[-1])+1))
                num_columns = paginate_count
            else:
                start = 'children:%s:' % (obj_type)
                num_columns = None

            obj_rows = self._cassandra_driver.multiget(
                                     self._OBJ_UUID_CF_NAME,
                                     parent_uuids,
                                     start=start,
                                     finish='children:%s;' % (obj_type),
                                     num_columns=num_columns,
                                     timestamp=True)

            def filter_rows_parent_anchor(sort=False):
                # flatten to [('children:<type>:<uuid>', (<val>,<ts>), *]
                all_cols = [cols for obj_key in list(obj_rows.keys())
                                 for cols in list(obj_rows[obj_key].items())]
                all_child_infos = {}
                for col_name, col_val_ts in all_cols:
                    # give chance for zk heartbeat/ping
                    gevent.sleep(0)
                    child_uuid = col_name.split(':')[2]
                    if obj_uuids and child_uuid not in obj_uuids:
                        continue
                    if back_ref_uuids:
                        child_cols = self._cassandra_driver.get(
                            self._OBJ_UUID_CF_NAME,
                            child_uuid,
                            start='ref:',
                            finish='ref;')
                        child_ref_ids = {col.split(':')[2]
                                         for col in child_cols or []}
                        if not set(back_ref_uuids) & child_ref_ids:
                            continue
                    all_child_infos[child_uuid] = {'uuid': child_uuid,
                                                   'tstamp': col_val_ts[1]}

                filt_child_infos = filter_rows(all_child_infos, filters)

                if not sort:
                    ret_child_infos = list(filt_child_infos.values())
                else:
                    ret_child_infos = sorted(list(filt_child_infos.values()),
                                             key=itemgetter('tstamp'))

                return get_fq_name_uuid_list(r['uuid'] for r in ret_child_infos)
            # end filter_rows_parent_anchor

            children_fq_names_uuids.extend(filter_rows_parent_anchor(sort=True))

        elif back_ref_uuids:
            # go from anchor to backrefs
            if paginate_start and paginate_start != '0':
                # get next lexical value of marker
                start = 'backref:%s:%s' % (obj_type,
                    paginate_start[:-1]+chr(ord(paginate_start[-1])+1))
                num_columns = paginate_count
            else:
                start = 'backref:%s:' % (obj_type)
                num_columns = None

            obj_rows = self._cassandra_driver.multiget(
                                     self._OBJ_UUID_CF_NAME,
                                     back_ref_uuids,
                                     start=start,
                                     finish='backref:%s;' % (obj_type),
                                     num_columns=num_columns,
                                     timestamp=True)

            def filter_rows_backref_anchor():
                # flatten to [('backref:<obj-type>:<uuid>', (<val>,<ts>), *]
                all_cols = [cols for obj_key in list(obj_rows.keys())
                            for cols in list(obj_rows[obj_key].items())]
                all_backref_infos = {}
                for col_name, col_val_ts in all_cols:
                    # give chance for zk heartbeat/ping
                    gevent.sleep(0)
                    backref_uuid = col_name.split(':')[2]
                    if obj_uuids and backref_uuid not in obj_uuids:
                        continue
                    all_backref_infos[backref_uuid] = \
                        {'uuid': backref_uuid, 'tstamp': col_val_ts[1]}

                filt_backref_infos = filter_rows(all_backref_infos, filters)
                return get_fq_name_uuid_list(r['uuid'] for r in
                                             list(filt_backref_infos.values()))
            # end filter_rows_backref_anchor

            children_fq_names_uuids.extend(filter_rows_backref_anchor())

        else:
            anchored_op = False
            if obj_uuids:
                # exact objects specified
                def filter_rows_object_list():
                    all_obj_infos = {}
                    marker = None
                    read_in = 0
                    start_idx = 0
                    if paginate_start and paginate_start != '0':
                        # paginate through objects
                        # in list order of obj_uuids
                        try:
                            start_idx = obj_uuids.index(paginate_start) + 1
                        except ValueError:
                            # simulate end of pagination
                            start_idx = len(obj_uuids)

                    for obj_uuid in obj_uuids[start_idx:]:
                        all_obj_infos[obj_uuid] = None
                        read_in += 1
                        if paginate_start and read_in >= paginate_count:
                            marker = obj_uuid
                            break

                    filt_obj_infos = filter_rows(all_obj_infos, filters, tag_filters)
                    return get_fq_name_uuid_list(list(filt_obj_infos.keys())), marker
                # end filter_rows_object_list

                filtered_rows, ret_marker = filter_rows_object_list()
                children_fq_names_uuids.extend(filtered_rows)

            else:  # grab all resources of this type
                obj_fq_name_cf = self._obj_fq_name_cf
                if paginate_start and paginate_start != '0':
                    start = paginate_start[:-1] + \
                                   chr(ord(paginate_start[-1]) + 1)
                else:
                    start = ''

                cols = obj_fq_name_cf.xget('%s' %(obj_type),
                                           column_start=start)

                def filter_rows_no_anchor():
                    marker = None
                    all_obj_infos = {}
                    read_in = 0
                    for col_name, _ in cols:
                        # give chance for zk heartbeat/ping
                        gevent.sleep(0)
                        col_name_arr = utils.decode_string(col_name).split(':')
                        obj_uuid = col_name_arr[-1]
                        all_obj_infos[obj_uuid] = (col_name_arr[:-1], obj_uuid)
                        read_in += 1
                        if paginate_start and read_in >= paginate_count:
                            marker = col_name
                            break

                    filt_obj_infos = filter_rows(all_obj_infos, filters, tag_filters)
                    return list(filt_obj_infos.values()), marker
                # end filter_rows_no_anchor

                filtered_rows, ret_marker = filter_rows_no_anchor()
                children_fq_names_uuids.extend(filtered_rows)

        if count:
            return (True, len(children_fq_names_uuids), None)

        # for anchored list with pagination,
        # prune from union of anchors and last uuid is marker
        if paginate_start and anchored_op:
            children_fq_names_uuids = sorted(children_fq_names_uuids,
                key=lambda fqn_uuid: fqn_uuid[1])
            if len(children_fq_names_uuids) > paginate_count:
                children_fq_names_uuids = children_fq_names_uuids[:paginate_count]

            if not children_fq_names_uuids:
                ret_marker = None
            else:
                ret_marker = children_fq_names_uuids[-1][1]

        return (True, children_fq_names_uuids, ret_marker)
    # end object_list

    def object_delete(self, obj_type, obj_uuid):
        obj_class = self._get_resource_class(obj_type)
        obj_uuid_cf = self._obj_uuid_cf
        fq_name = self._cassandra_driver.get_one_col(self._OBJ_UUID_CF_NAME,
                                                     obj_uuid,
                                                     'fq_name')
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
        symmetric_ref_updates = []
        for (col_name, col_val) in col_name_iter:
            (_, ref_type, ref_uuid) = col_name.split(':')
            ret = self._delete_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid)
            symmetric_ref_updates.extend(ret)

        # remove link from relaxed back refs
        col_start = 'relaxbackref:'
        col_fin = 'relaxbackref;'
        col_name_iter = obj_uuid_cf.xget(
            obj_uuid, column_start=col_start, column_finish=col_fin)
        for (col_name, col_val) in col_name_iter:
            (_, backref_uuid) = col_name.split(':')
            self._delete_ref(bch, None, backref_uuid, obj_type, obj_uuid)

        self._cassandra_driver.remove(obj_uuid, batch=bch)
        try:
            bch.send()
        finally:
            self._obj_cache_mgr.evict(obj_type, [obj_uuid])

        # Update fqname table
        fq_name_str = ':'.join(fq_name)
        fq_name_col = utils.encode_string(fq_name_str) + ':' + obj_uuid
        self._obj_fq_name_cf.remove(obj_type,
                                    columns = [fq_name_col])

        # Purge map naming cache
        self.cache_uuid_to_fq_name_del(obj_uuid)

        return (True, symmetric_ref_updates)
    # end object_delete

    def prop_collection_read(self, obj_type, obj_uuid, obj_fields, position):
        obj_class = self._get_resource_class(obj_type)

        result = {}
        # always read-in id-perms for upper-layers to do rbac/visibility
        result['id_perms'] = self._cassandra_driver.get_one_col(
                                            self._OBJ_UUID_CF_NAME,
                                            obj_uuid,
                                            'prop:id_perms')

        # read in prop-list or prop-map fields
        for field in obj_fields:
            if field in obj_class.prop_list_fields:
                prop_pfx = 'propl'
            elif field in obj_class.prop_map_fields:
                prop_pfx = 'propm'
            else:
                continue
            if position:
                col_start = '%s:%s:%s' % (prop_pfx, field, position)
                col_end = '%s:%s:%s' % (prop_pfx, field, position)
            else:
                col_start = '%s:%s:' % (prop_pfx, field)
                col_end = '%s:%s;' % (prop_pfx, field)

            obj_cols = self._obj_uuid_cf.xget(obj_uuid,
                                              column_start=col_start,
                                              column_finish=col_end)

            result[field] = []
            for name, value in obj_cols:
                # tuple of col_value, position. result is already sorted
                # lexically by position (necessary only for list property)
                result[field].append((json.loads(value), name.split(':')[-1]))

        return (True, result)
    # end prop_collection_read

    def cache_uuid_to_fq_name_add(self, id, fq_name, obj_type):
        self._cache_uuid_to_fq_name[id] = (fq_name, obj_type)
    # end cache_uuid_to_fq_name_add

    def cache_uuid_to_fq_name_del(self, id):
        self._cache_uuid_to_fq_name.pop(id, None)
    # end cache_uuid_to_fq_name_del

    def uuid_to_fq_name(self, id):
        try:
            return copy.copy(self._cache_uuid_to_fq_name[id][0])
        except KeyError:
            obj = self._cassandra_driver.get(self._OBJ_UUID_CF_NAME, id,
                                             columns=['fq_name', 'type'])
            if not obj:
                raise NoIdError(id)
            if 'type' not in obj or 'fq_name' not in obj:
                raise NoIdError(id)
            fq_name = obj['fq_name']
            obj_type = obj['type']
            self.cache_uuid_to_fq_name_add(id, fq_name, obj_type)
            return copy.copy(fq_name)
    # end uuid_to_fq_name

    def uuid_to_obj_type(self, id):
        try:
            return self._cache_uuid_to_fq_name[id][1]
        except KeyError:
            obj = self._cassandra_driver.get(self._OBJ_UUID_CF_NAME, id,
                           columns=['fq_name', 'type'])
            if not obj:
                raise NoIdError(id)
            if 'type' not in obj or 'fq_name' not in obj:
                raise NoIdError(id)
            fq_name = obj['fq_name']
            obj_type = obj['type']
            self.cache_uuid_to_fq_name_add(id, fq_name, obj_type)
            return obj_type
    # end uuid_to_obj_type

    def fq_name_to_uuid(self, obj_type, fq_name):
        fq_name_str = utils.encode_string(':'.join(fq_name))

        col_infos = self._cassandra_driver.get(self._OBJ_FQ_NAME_CF_NAME,
                             obj_type,
                             start=fq_name_str + ':',
                             finish=fq_name_str + ';')
        if not col_infos:
            raise NoIdError('%s %s' % (obj_type, fq_name_str))
        if len(col_infos) > 1:
            raise VncError('Multi match %s for %s' % (fq_name_str, obj_type))
        fq_name_uuid = utils.decode_string(col_infos.popitem()[0]).split(':')
        if obj_type != 'route_target' and fq_name_uuid[:-1] != fq_name:
            raise NoIdError('%s %s' % (obj_type, fq_name_str))
        return fq_name_uuid[-1]
    # end fq_name_to_uuid

    # return all objects shared with a (share_type, share_id)
    def get_shared(self, obj_type, share_id='', share_type='global'):
        result = []
        column = '%s:%s' % (share_type, share_id)

        col_infos = self._cassandra_driver.get(self._OBJ_SHARED_CF_NAME,
                             obj_type,
                             start=column + ':',
                             finish=column + ';')

        if not col_infos:
            return None

        for (col_name, col_val) in list(col_infos.items()):
            # ('*:*:f7963198-08a4-4b96-a02e-41cc66593163', u'7')
            obj_uuid = col_name.split(':')[-1]
            result.append((obj_uuid, col_val))

        return result

    # share an object 'obj_id' with <share_type:share_id>
    # rwx indicate type of access (sharing) allowed
    def set_shared(self, obj_type, obj_id, share_id = '', share_type = 'global', rwx = 7):
        col_name = '%s:%s:%s' % (share_type, share_id, obj_id)
        self._cassandra_driver._obj_shared_cf.insert(obj_type,
                                                     {col_name:json.dumps(rwx)})

    # delete share of 'obj_id' object with <share_type:share_id>
    def del_shared(self, obj_type, obj_id, share_id = '', share_type = 'global'):
        col_name = '%s:%s:%s' % (share_type, share_id, obj_id)
        self._cassandra_driver._obj_shared_cf.remove(obj_type,
                                                     columns=[col_name])

    def _render_obj_from_db(self, obj_class, obj_rows, field_names=None,
                            include_backrefs_children=False):
        ref_fields = obj_class.ref_fields
        backref_fields = obj_class.backref_fields
        children_fields = obj_class.children_fields
        list_fields = obj_class.prop_list_fields
        map_fields = obj_class.prop_map_fields
        prop_fields = obj_class.prop_fields - (list_fields | map_fields)

        results = {}
        for obj_uuid, obj_cols in list(obj_rows.items()):
            if 'type' not in obj_cols or 'fq_name' not in obj_cols:
                # if object has been deleted, these fields may not
                # be present
                continue
            if obj_class.object_type != obj_cols.pop('type')[0]:
                continue
            id_perms_ts = 0
            row_latest_ts = 0
            result = {}
            result['uuid'] = obj_uuid
            result['fq_name'] = obj_cols.pop('fq_name')[0]
            for col_name in list(obj_cols.keys()):
                if self._is_parent(col_name):
                    # non config-root child
                    (_, _, parent_uuid) = col_name.split(':')
                    try:
                        result['parent_type'] = obj_cols['parent_type'][0]
                    except KeyError:
                        # parent_type may not be present in obj_cols
                        pass
                    result['parent_uuid'] = parent_uuid
                    continue

                if self._is_prop(col_name):
                    (_, prop_name) = col_name.split(':')
                    if prop_name == 'id_perms':
                        id_perms_ts = obj_cols[col_name][1]
                    if ((prop_name not in prop_fields) or
                        (field_names and prop_name not in field_names)):
                        continue
                    result[prop_name] = obj_cols[col_name][0]
                    continue

                if self._is_prop_list(col_name):
                    (_, prop_name, prop_elem_position) = col_name.split(':')
                    if field_names and prop_name not in field_names:
                        continue
                    if obj_class.prop_list_field_has_wrappers[prop_name]:
                        prop_field_types = obj_class.prop_field_types[prop_name]
                        wrapper_type = prop_field_types['xsd_type']
                        wrapper_cls = self._get_xsd_class(wrapper_type)
                        wrapper_field = wrapper_cls.attr_fields[0]
                        if prop_name not in result:
                            result[prop_name] = {wrapper_field: []}
                        result[prop_name][wrapper_field].append(
                            (obj_cols[col_name][0], prop_elem_position))
                    else:
                        if prop_name not in result:
                            result[prop_name] = []
                        result[prop_name].append((obj_cols[col_name][0],
                                                  prop_elem_position))
                    continue

                if self._is_prop_map(col_name):
                    (_, prop_name, _) = col_name.split(':')
                    if field_names and prop_name not in field_names:
                        continue
                    if obj_class.prop_map_field_has_wrappers[prop_name]:
                        prop_field_types = obj_class.prop_field_types[prop_name]
                        wrapper_type = prop_field_types['xsd_type']
                        wrapper_cls = self._get_xsd_class(wrapper_type)
                        wrapper_field = wrapper_cls.attr_fields[0]
                        if prop_name not in result:
                            result[prop_name] = {wrapper_field: []}
                        result[prop_name][wrapper_field].append(
                            obj_cols[col_name][0])
                    else:
                        if prop_name not in result:
                            result[prop_name] = []
                        result[prop_name].append(obj_cols[col_name][0])
                    continue

                if self._is_children(col_name):
                    (_, child_type, child_uuid) = col_name.split(':')
                    if field_names and '%ss' %(child_type) not in field_names:
                        continue
                    if child_type+'s' not in children_fields:
                        continue

                    child_tstamp = obj_cols[col_name][1]
                    try:
                        self._read_child(result, obj_uuid, child_type,
                                         child_uuid, child_tstamp)
                    except NoIdError:
                        continue
                    continue

                if self._is_ref(col_name):
                    (_, ref_type, ref_uuid) = col_name.split(':')
                    if ((ref_type+'_refs' not in ref_fields) or
                        (field_names and ref_type + '_refs' not in field_names)):
                        continue
                    self._read_ref(result, obj_uuid, ref_type, ref_uuid,
                                   obj_cols[col_name][0])
                    continue

                if self._is_backref(col_name):
                    (_, back_ref_type, back_ref_uuid) = col_name.split(':')
                    if back_ref_type+'_back_refs' not in backref_fields:
                        continue
                    if (field_names and
                        '%s_back_refs' %(back_ref_type) not in field_names):
                        continue

                    try:
                        self._read_back_ref(result, obj_uuid, back_ref_type,
                                            back_ref_uuid, obj_cols[col_name][0])
                    except NoIdError:
                        continue
                    continue

                if self._is_metadata(col_name):
                    (_, meta_type) = col_name.split(':')
                    if meta_type == 'latest_col_ts':
                        row_latest_ts = obj_cols[col_name][1]
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

            # Ordering property lists by position attribute
            for prop_name in (obj_class.prop_list_fields & set(result.keys())):
                if isinstance(result[prop_name], list):
                    result[prop_name] = [el[0] for el in
                                         sorted(result[prop_name],
                                                key=itemgetter(1))]
                elif isinstance(result[prop_name], dict):
                    wrapper, unsorted_list = result[prop_name].popitem()
                    result[prop_name][wrapper] = [el[0] for el in
                                                  sorted(unsorted_list,
                                                         key=itemgetter(1))]

            # 'id_perms_ts' tracks timestamp of id-perms column
            #  i.e. latest update of *any* prop or ref.
            # 'row_latest_ts' tracks timestamp of last modified column
            # so any backref/children column is also captured. 0=>unknown
            results[obj_uuid] = {'obj_dict': result,
                                 'id_perms_ts': id_perms_ts}
            if include_backrefs_children:
                # update our copy of ts only if we read the
                # corresponding fields from db
                results[obj_uuid]['row_latest_ts'] = row_latest_ts
        # end for all rows

        return results
    # end _render_obj_from_db

    def _read_child(self, result, obj_uuid, child_obj_type, child_uuid,
                    child_tstamp):
        if '%ss' % (child_obj_type) not in result:
            result['%ss' % (child_obj_type)] = []
        child_res_type = self._get_resource_class(child_obj_type).resource_type

        child_info = {}
        child_info['to'] = self.uuid_to_fq_name(child_uuid)
        child_info['uuid'] = child_uuid
        child_info['tstamp'] = child_tstamp

        result['%ss' % (child_obj_type)].append(child_info)
    # end _read_child

    def _read_ref(self, result, obj_uuid, ref_obj_type, ref_uuid, ref_data_json):
        if '%s_refs' % (ref_obj_type) not in result:
            result['%s_refs' % (ref_obj_type)] = []

        ref_data = ref_data_json
        ref_info = {}
        try:
            ref_info['to'] = self.uuid_to_fq_name(ref_uuid)
        except NoIdError:
            ref_info['to'] = ['ERROR']

        if ref_data:
            try:
                ref_info['attr'] = ref_data['attr']
            except KeyError:
                # TODO remove backward compat old format had attr directly
                ref_info['attr'] = ref_data

        ref_info['uuid'] = ref_uuid

        result['%s_refs' % (ref_obj_type)].append(ref_info)
    # end _read_ref

    def _read_back_ref(self, result, obj_uuid, back_ref_obj_type, back_ref_uuid,
                       back_ref_data_json):
        if '%s_back_refs' % (back_ref_obj_type) not in result:
            result['%s_back_refs' % (back_ref_obj_type)] = []

        back_ref_info = {}
        back_ref_info['to'] = self.uuid_to_fq_name(back_ref_uuid)
        back_ref_data = back_ref_data_json
        if back_ref_data:
            try:
                back_ref_info['attr'] = back_ref_data['attr']
            except KeyError:
                # TODO remove backward compat old format had attr directly
                back_ref_info['attr'] = back_ref_data

        back_ref_info['uuid'] = back_ref_uuid

        result['%s_back_refs' % (back_ref_obj_type)].append(back_ref_info)
    # end _read_back_ref

    def walk(self, fn=None):
        type_to_object = {}
        for obj_uuid, obj_col in self._obj_uuid_cf.get_range(
                columns=['type', 'fq_name']):
            try:
                obj_type = json.loads(obj_col['type'])
                obj_fq_name = json.loads(obj_col['fq_name'])
                # prep cache to avoid n/w round-trip in db.read for ref
                self.cache_uuid_to_fq_name_add(obj_uuid, obj_fq_name, obj_type)

                try:
                    type_to_object[obj_type].append(obj_uuid)
                except KeyError:
                    type_to_object[obj_type] = [obj_uuid]
            except Exception as e:
                self._logger('Error in db walk read %s' % (str(e)),
                             level=SandeshLevel.SYS_ERR)
                continue

        if fn is None:
            return []
        walk_results = []
        for obj_type, uuid_list in list(type_to_object.items()):
            try:
                self._logger('DB walk: obj_type %s len %s'
                             % (obj_type, len(uuid_list)),
                             level=SandeshLevel.SYS_INFO)
                result = fn(obj_type, uuid_list)
                if result:
                    walk_results.append(result)
            except Exception as e:
                self._logger('Error in db walk invoke %s' % (str(e)),
                             level=SandeshLevel.SYS_ERR)
                continue

        return walk_results
    # end walk
# end class VncCassandraClient


class ObjectCacheManager(object):
    class CachedObject(object):
        # provide a read-only copy in so far as
        # top level keys cannot be add/mod/del
        class RODict(dict):
            def __readonly__(self, *args, **kwargs):
                raise RuntimeError("Cannot modify ReadOnlyDict")
            __setitem__ = __readonly__
            __delitem__ = __readonly__
            pop = __readonly__
            popitem = __readonly__
            clear = __readonly__
            update = __readonly__
            setdefault = __readonly__
            del __readonly__
        # end RODict

        def __init__(self, obj_dict, id_perms_ts, row_latest_ts):
            self.obj_dict = self.RODict(obj_dict)
            self.id_perms_ts = id_perms_ts
            self.row_latest_ts = row_latest_ts
        # end __init__

        def update_obj_dict(self, new_obj_dict):
            self.obj_dict = self.RODict(new_obj_dict)
        # end update_obj_dict

        def get_filtered_copy(self, field_names=None):
            if not field_names:
                return self.obj_dict

            # TODO filter with field_names
            return {k: copy.deepcopy(self.obj_dict[k])
                    for k in field_names if k in self.obj_dict}
        # end get_filtered_copy

    # end class CachedObject

    def __init__(self, logger, db_client, max_entries,
                 obj_cache_exclude_types=None, debug_obj_cache_types=None):
        self._logger = logger
        self.max_entries = max_entries
        self._db_client = db_client
        self._cache = OrderedDict()
        self._obj_cache_exclude_types = set(obj_cache_exclude_types or [])
        self._debug_obj_cache_types = set(debug_obj_cache_types or [])
        self._debug_obj_cache_types -= self._obj_cache_exclude_types
    # end __init__

    def _log(self, msg, level=SandeshLevel.SYS_DEBUG):
        msg = 'Object UUID cache manager: %s' % msg
        self._logger(msg, level)

    def evict(self, obj_type, obj_uuids):
        for obj_uuid in obj_uuids:
            try:
                obj_dict = self._cache.pop(obj_uuid).obj_dict
                if obj_type in self._debug_obj_cache_types:
                    self._log("%s %s (%s) was evicted from cache. Cache "
                              "contained: %s" % (
                                  obj_type.replace('_', '-').title(),
                                  ':'.join(obj_dict['fq_name']),
                                  obj_uuid,
                                  pformat(obj_dict),
                              ),
                             )
            except KeyError:
                continue
    # end evict

    def set(self, obj_type, db_rendered_objs, req_fields,
            include_backrefs_children):

        # build up results with field filter
        result_obj_dicts = []
        if req_fields:
            result_fields = set(req_fields) | set(['fq_name', 'uuid',
                 'parent_type', 'parent_uuid'])

        for obj_uuid, render_info in list(db_rendered_objs.items()):
            id_perms_ts = render_info.get('id_perms_ts', 0)
            row_latest_ts = render_info.get('row_latest_ts', 0)
            cached_obj = self._cache.pop(obj_uuid, None)
            if cached_obj is not None:
                # if we had stale, just update from new db value
                cached_obj.update_obj_dict(render_info['obj_dict'])
                cached_obj.id_perms_ts = id_perms_ts
                if include_backrefs_children:
                    cached_obj.row_latest_ts = row_latest_ts
            else:
                # this was a miss in cache
                cached_obj = self.CachedObject(
                    render_info['obj_dict'],
                    id_perms_ts,
                    row_latest_ts,
                )

            if len(self._cache) >= self.max_entries:
                # get first element (least recently used)
                # without getting full copy of dict keys
                key = next(iter(list(self._cache.iterkeys())))
                self.evict(obj_type, [key])

            self._cache[obj_uuid] = cached_obj
            if obj_type in self._debug_obj_cache_types:
                self._log("%s %s (%s) was set in cache with values: %s" % (
                              obj_type.replace('_', ' ').title(),
                              ':'.join(cached_obj.obj_dict['fq_name']),
                              obj_uuid,
                              pformat(cached_obj.obj_dict),
                          ),
                         )
            if req_fields:
                result_obj_dicts.append(
                    cached_obj.get_filtered_copy(result_fields))
            else:
                result_obj_dicts.append(cached_obj.get_filtered_copy())
        # end for all rendered objects

        return result_obj_dicts
    # end set

    def read(self, obj_class, obj_uuids, req_fields, include_backrefs_children):
        # find which keys are a hit, find which hit keys are not stale
        # return hit entries and miss+stale uuids.
        hit_uuids = []
        miss_uuids = []
        for obj_uuid in obj_uuids:
            if obj_uuid in self._cache:
                hit_uuids.append(obj_uuid)
            else:
                miss_uuids.append(obj_uuid)

        stale_uuids = []

        # staleness when include_backrefs_children is False = id_perms tstamp
        #     when include_backrefs_children is True = latest_col_ts tstamp
        if include_backrefs_children:
            stale_check_col_name = 'META:latest_col_ts'
            stale_check_ts_attr = 'row_latest_ts'
        else:
            stale_check_col_name = 'prop:id_perms'
            stale_check_ts_attr = 'id_perms_ts'

        hit_rows_in_db = self._db_client._cassandra_driver.multiget(
            self._db_client._OBJ_UUID_CF_NAME, hit_uuids,
            columns=[stale_check_col_name], timestamp=True)

        obj_dicts = []
        result_fields = {'fq_name', 'uuid', 'parent_type', 'parent_uuid'}
        if req_fields:
            result_fields = set(req_fields) | result_fields

        for hit_uuid in hit_uuids:
            try:
                obj_cols = hit_rows_in_db[hit_uuid]
                cached_obj = self._cache[hit_uuid]
            except KeyError:
                # Either stale check column missing, treat as miss
                # Or entry could have been evicted while context switched
                # for reading stale-check-col, treat as miss
                miss_uuids.append(hit_uuid)
                continue

            if (getattr(cached_obj, stale_check_ts_attr) !=
                    obj_cols[stale_check_col_name][1]):
                miss_uuids.append(hit_uuid)
                stale_uuids.append(hit_uuid)
                continue

            if req_fields:
                obj_dicts.append(cached_obj.get_filtered_copy(result_fields))
            else:
                obj_dicts.append(cached_obj.get_filtered_copy())

            if obj_class.object_type in self._debug_obj_cache_types:
                obj_rows = self._db_client._cassandra_driver.multiget(
                    self._db_client._OBJ_UUID_CF_NAME,
                    [hit_uuid],
                    timestamp=True)
                rendered_objs = self._db_client._render_obj_from_db(
                    obj_class, obj_rows, req_fields, include_backrefs_children)
                db_obj_dict = rendered_objs[hit_uuid]['obj_dict']
                self._log("%s %s (%s) was read from cache.\nDB values: %s\n"
                          "Cache value: %s\n" % (
                              obj_class.object_type.replace('_', ' ').title(),
                              ':'.join(cached_obj.obj_dict['fq_name']),
                              hit_uuid,
                              pformat(db_obj_dict),
                              pformat(cached_obj.obj_dict),
                          ),
                         )
        # end for all hit in cache

        self.evict(obj_class.object_type, stale_uuids)
        return obj_dicts, miss_uuids
    # end read

    def dump_cache(self, obj_uuids=None, count=10):
        obj_dicts = {}
        i = 1
        if obj_uuids:
            for obj_uuid in obj_uuids:
                try:
                    obj = self._cache[obj_uuid]
                except KeyError:
                    continue
                obj_json = json.dumps(obj, default=lambda o: dict((k, v)
                                      for k, v in list(o.__dict__.items())))
                obj_dicts[i] = json.loads(obj_json)
                i += 1
        else:
            for key in self._cache:
                if i > count:
                    break
                obj = self._cache[key]
                obj_json = json.dumps(obj, default=lambda o: dict((k, v)
                                      for k, v in list(o.__dict__.items())))
                obj_dicts[i] = json.loads(obj_json)
                i += 1
        return obj_dicts

# end class ObjectCacheManager
