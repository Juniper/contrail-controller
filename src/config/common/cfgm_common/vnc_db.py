#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of database model for contrail config daemons
"""
from __future__ import absolute_import
from __future__ import unicode_literals
from builtins import object
from future.utils import with_metaclass
from past.builtins import basestring
from vnc_api.gen.resource_client import *

from .exceptions import NoIdError
from .utils import obj_type_to_vnc_class, compare_refs


class DBBaseMeta(type):
    def __iter__(cls):
        for i in cls._dict:
            yield i

    def keys(cls):
        for i in list(cls._dict.keys()):
            yield i

    def values(cls):
        for i in list(cls._dict.values()):
            yield i

    def items(cls):
        for i in list(cls._dict.items()):
            yield i

    def __contains__(cls, item):
        # check for 'item in cls'
        return item in cls._dict


class DBBase(object, with_metaclass(DBBaseMeta)):
    # This is the base class for all DB objects. All derived objects must
    # have a class member called _dict of dictionary type.
    # The init method of this class must be callled before using any functions

    _logger = None
    _object_db = None
    _manager = None

    # objects in the database could be indexed by uuid or fq-name
    # set _indexed_by_name to True in the derived class to use fq-name as index
    _indexed_by_name = False

    @classmethod
    def init(cls, manager, logger, object_db):
        cls._logger = logger
        cls._object_db = object_db
        cls._manager = manager
    # end init

    @classmethod
    def clear(cls):
        cls._logger = None
        cls._object_db = None
        cls._manager = None
    # end clear

    @classmethod
    def get(cls, key):
        return cls._dict.get(key)
    # end get

    @classmethod
    def locate(cls, key, *args):
        if key not in cls._dict:
            try:
                obj = cls(key, *args)
                cls._dict[key] = obj
                return obj
            except NoIdError as e:
                cls._logger.debug(
                    "Exception %s while creating %s for %s" %
                    (e, cls.__name__, key))
                return None
        return cls._dict[key]
    # end locate

    def delete_obj(self):
        # Override in derived class to provide additional functionality
        pass

    @classmethod
    def delete(cls, key):
        try:
            obj = cls.get(key)
            if obj is None:
                return
            obj.delete_obj()
        finally:
            # Even if an error is encountered, delete the object from cache
            try:
                del cls._dict[key]
            except KeyError:
                pass
    # end delete

    def get_ref_uuid_from_dict(self, obj_dict, ref_name):
        if ref_name in obj_dict:
            return obj_dict[ref_name][0]['uuid']
        else:
            return None

    def get_key(self):
        if self._indexed_by_name:
            return self.name
        return self.uuid
    # end get_key

    def add_ref(self, ref_type, ref, attr=None):
        if hasattr(self, ref_type):
            setattr(self, ref_type, ref)
        elif hasattr(self, ref_type+'s'):
            ref_set = getattr(self, ref_type+'s')
            if isinstance(ref_set, set):
                ref_set.add(ref)
            elif isinstance(ref_set, dict):
                ref_set[ref] = attr
    # end add_ref

    def delete_ref(self, ref_type, ref):
        if hasattr(self, ref_type) and getattr(self, ref_type) == ref:
            setattr(self, ref_type, None)
        elif hasattr(self, ref_type+'s'):
            ref_set = getattr(self, ref_type+'s')
            if isinstance(ref_set, set):
                ref_set.discard(ref)
            elif isinstance(ref_set, dict) and ref in ref_set:
                del ref_set[ref]
    # end delete_ref

    def add_to_parent(self, obj):
        if isinstance(obj, dict):
            parent_type = obj.get('parent_type')
        else:
            parent_type = obj.parent_type
        self.parent_type = parent_type.replace('-', '_')
        if self._indexed_by_name:
            if isinstance(obj, dict):
                fq_name = obj.get('fq_name', [])
                if fq_name:
                    self.parent_key = ':'.join(fq_name[:-1])
                else:
                    return
            else:
                self.parent_key = obj.get_parent_fq_name_str()
        else:
            if isinstance(obj, dict):
                self.parent_key = obj.get('parent_uuid')
            else:
                self.parent_key = obj.get_parent_uuid(obj)
        if not self.parent_type or not self.parent_key:
            return
        self.add_ref(self.parent_type, self.parent_key)
        p_obj = self.get_obj_type_map()[self.parent_type].get(self.parent_key)
        if p_obj is not None:
            p_obj.add_ref(self.obj_type, self.get_key())
    # end

    def remove_from_parent(self):
        if not self.parent_type or not self.parent_key:
            return
        p_obj = self.get_obj_type_map()[self.parent_type].get(self.parent_key)
        if p_obj is not None:
            p_obj.delete_ref(self.obj_type, self.get_key())

    def _get_ref_key(self, ref, ref_type=None):
        if self._indexed_by_name:
            key = ':'.join(ref['to'])
        else:
            try:
                key = ref['uuid']
            except KeyError:
                fq_name = ref['to']
                key = self._object_db.fq_name_to_uuid(ref_type, fq_name)
        return key
    # end _get_ref_key

    def get_single_ref_attr(self, ref_type, obj):
        if isinstance(obj, dict):
            refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        else:
            refs = (getattr(obj, ref_type+'_refs', None) or
                    getattr(obj, ref_type+'_back_refs', None))

        if refs:
            ref_attr = refs[0].get('attr', None)
            return ref_attr
        return None
    # end get_single_ref_attr

    # Update a single ref. Return True if any update was made
    def update_single_ref(self, ref_type, obj):
        if isinstance(obj, dict):
            refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        else:
            refs = (getattr(obj, ref_type+'_refs', None) or
                    getattr(obj, ref_type+'_back_refs', None))

        if refs:
            new_key = self._get_ref_key(refs[0], ref_type)
        else:
            new_key = None
        old_key = getattr(self, ref_type, None)
        if old_key == new_key:
            return False
        ref_cls = self.get_obj_type_map().get(ref_type)
        if ref_cls:
            ref_obj = ref_cls.get(old_key)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.get_key())
            ref_obj = ref_cls.get(new_key)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.get_key())
        setattr(self, ref_type, new_key)
        return True
    # end update_single_ref

    def set_children(self, ref_type, obj):
        if isinstance(obj, dict):
            refs = obj.get(ref_type+'s')
        else:
            refs = getattr(obj, ref_type+'s', None)
        new_refs = set()
        for ref in refs or []:
            new_key = self._get_ref_key(ref, ref_type)
            new_refs.add(new_key)
        setattr(self, ref_type+'s', new_refs)
    # end set_children

    # Update a multiple refs. Return True if any update was made
    def update_multiple_refs(self, ref_type, obj):
        if isinstance(obj, dict):
            refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        else:
            refs = (getattr(obj, ref_type+'_refs', None) or
                    getattr(obj, ref_type+'_back_refs', None))

        new_refs = set()
        for ref in refs or []:
            new_key = self._get_ref_key(ref, ref_type)
            new_refs.add(new_key)
        old_refs = getattr(self, ref_type+'s')
        if old_refs == new_refs:
            return False
        for ref_key in old_refs - new_refs:
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.get_key())
        for ref_key in new_refs - old_refs:
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.get_key())
        setattr(self, ref_type+'s', new_refs)
        return True
    # end update_multiple_refs

    def update_refs(self, ref_type, obj):
        if hasattr(self, ref_type):
            return self.update_single_ref(ref_type, obj)
        elif isinstance(getattr(self, ref_type+'s', None), set):
            return self.update_multiple_refs(ref_type, obj)
        elif isinstance(getattr(self, ref_type+'s', None), dict):
            return self.update_multiple_refs_with_attr(ref_type, obj)

    def update_multiple_refs_with_attr(self, ref_type, obj):
        if isinstance(obj, dict):
            refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        else:
            refs = (getattr(obj, ref_type+'_refs', None) or
                    getattr(obj, ref_type+'_back_refs', None))

        new_refs = {}
        for ref in refs or []:
            new_key = self._get_ref_key(ref, ref_type)
            new_refs[new_key] = ref.get('attr')
        old_refs = getattr(self, ref_type+'s')
        update = False
        for ref_key in set(old_refs.keys()) - set(new_refs.keys()):
            update = True
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.get_key())
        for ref_key in new_refs:
            if ref_key in old_refs and new_refs[ref_key] == old_refs[ref_key]:
                continue
            update = True
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.get_key(), new_refs[ref_key])
        setattr(self, ref_type+'s', new_refs)
        return update
    # end update_multiple_refs

    @classmethod
    def read_obj(cls, uuid, obj_type=None, fields=None):
        ok, objs = cls._object_db.object_read(obj_type or cls.obj_type, [uuid],
                                              field_names=fields)
        if not ok:
            cls._logger.error(
                'Cannot read %s %s, error %s' % (obj_type, uuid, objs))
            raise NoIdError(uuid)
        return objs[0]
    # end read_obj

    @classmethod
    def vnc_obj_from_dict(cls, obj_type, obj_dict):
        cls = obj_type_to_vnc_class(obj_type, __name__)
        return cls.from_dict(**obj_dict)

    @classmethod
    def read_vnc_obj(cls, uuid=None, fq_name=None, obj_type=None, fields=None):
        if uuid is None and fq_name is None:
            raise NoIdError('')
        obj_type = obj_type or cls.obj_type
        if uuid is None:
            if isinstance(fq_name, basestring):
                fq_name = fq_name.split(':')
            uuid = cls._object_db.fq_name_to_uuid(obj_type, fq_name)
        obj_dict = cls.read_obj(uuid, obj_type, fields)
        obj = cls.vnc_obj_from_dict(obj_type, obj_dict)
        obj.clear_pending_updates()
        return obj
    # end read_vnc_obj

    def update_vnc_obj(self, obj=None):
        if obj:
            old_obj = None
            self.obj = obj
        else:
            old_obj = getattr(self, 'obj', None)
            uuid = getattr(self, 'uuid', None)
            if uuid:
                self.obj = self.read_vnc_obj(uuid=uuid)
            else:
                self.obj = self.read_vnc_obj(fq_name=self.name)

        changed = []
        for field in self.ref_fields or []:
            old_field = getattr(old_obj, field+'_refs', None)
            new_field = getattr(self.obj, field+'_refs', None)
            if compare_refs(old_field, new_field):
                continue
            self.update_refs(field, self.obj)
            changed.append(field)
        for field in self.prop_fields or []:
            old_field = getattr(old_obj, field, None)
            new_field = getattr(self.obj, field, None)
            if old_field == new_field:
                continue
            if hasattr(self, field):
                setattr(self, field, new_field)
            changed.append(field)
        return changed

    @classmethod
    def list_obj(cls, obj_type=None, fields=None):
        obj_type = obj_type or cls.obj_type
        ok, result, _ = cls._object_db.object_list(obj_type)
        if not ok:
            return []
        uuids = [uuid for _, uuid in result]
        try:
            ok, objs = cls._object_db.object_read(obj_type, uuids,
                                                field_names=fields)
            if not ok:
                return []
        except NoIdError:
            return []
        return objs

    @classmethod
    def list_vnc_obj(cls, obj_type=None, fields=None):
        obj_type = obj_type or cls.obj_type
        vnc_cls = obj_type_to_vnc_class(obj_type, __name__)
        obj_dicts = cls.list_obj(obj_type, fields)
        for obj_dict in obj_dicts:
            obj = vnc_cls.from_dict(**obj_dict)
            obj.clear_pending_updates()
            yield obj

    def get_parent_uuid(self, obj):
        if 'parent_uuid' in obj:
            return obj['parent_uuid']
        elif 'parent_type' in obj:
            parent_type = obj['parent_type'].replace('-', '_')
            parent_fq_name = obj['fq_name'][:-1]
            return self._object_db.fq_name_to_uuid(parent_type, parent_fq_name)
        else:
            return None
    # end get_parent_uuid

    @classmethod
    def find_by_name_or_uuid(cls, name_or_uuid):
        obj = cls.get(name_or_uuid)
        if obj:
            return obj

        for obj in list(cls.values()):
            if obj.name == name_or_uuid:
                return obj
        return None
    # end find_by_name_or_uuid

    @classmethod
    def find_by_fq_name(cls, fq_name):
        for obj in list(cls.values()):
            if obj.fq_name == fq_name:
                return obj
        return None

    @classmethod
    def reset(cls):
        cls._dict = {}

    @classmethod
    def get_obj_type_map(cls):
        module_base = [x for x in DBBase.__subclasses__()
                       if cls.__module__ == x.obj_type]
        return dict((x.obj_type, x) for x in module_base[0].__subclasses__())

    @classmethod
    def get_by_uuid(cls, uuid, *args):
        name_or_uuid = uuid
        try:
            if cls._indexed_by_name:
                fq_name = cls._object_db.uuid_to_fq_name(uuid)
                name_or_uuid = ':'.join(fq_name)
            return cls.get(name_or_uuid)
        except NoIdError:
            return None

    def skip_evaluate(self, from_type):
        return False
    #end skip_evaluate
# end class DBBase


class FeatureFlagBase(object):
    @classmethod
    def set_version(cls, version):
        cls._ff_dict['release_version'] = version

    @classmethod
    def is_feature_enabled(cls, feature_id, version=None):
        # if feature is not present, return false
        # otherwise, return enable_feature
        if not version:
            version = cls._ff_dict['release_version']
        version_fflags = cls._ff_dict['feature_flags'].get(version, {})
        fflag = version_fflags.get(feature_id, {})
        enabled = fflag.get('enable_feature', False)
        return enabled
    # end is_feature_enabled

    #Register per object type for a given feature
    @classmethod
    def register_callback(cls, feature_id, obj_type, cb, params):
        if not feature_id:
           return False

        # If object type is not specified, make default object type
        # as feature id name
        if not obj_type:
           obj_type = feature_id

        # parameters for callback should be arg list or kwargs
        if not (isinstance(params, list) or isinstance(params, dict)):
           return False
        cbs = cls._ff_dict['callbacks']
        feature_cbs = cbs.setdefault(feature_id, {})

        # Register callback for object type one time only
        if feature_cbs.get(obj_type):
           return False
        # store callbacks in _ff_dict
        feature_cbs[obj_type] = (cb, params)
        return True
    # end register_cb


    @classmethod
    def _execute_callbacks(cls, feature_id):
        feature_cbs = cls._ff_dict['callbacks'].get(feature_id)
        if not feature_cbs:
           return
        # run callbacks one by one
        for obj_type, cb_tuple in feature_cbs.items():
            if not cb_tuple:
               continue
            cb, params = cb_tuple
            if isinstance(params, list):
               cb(*params)
            elif isinstance(params, dict):
               cb(**params)
            else:
               cb(params)
        return
    # end _execute_callbacks


    @classmethod
    def feature_flag_update(cls, feature_id, version, old_fflag, new_fflag, execute_cbs=True):
        # run callbacks, only if the enable_feature is changed.
        # otherwise, simply update feature flags without invoking callbacks
        # Caller also has an option to pass execute_cbs, if don't want to run callbacks
        # during the flag update
        update = False
        if old_fflag and \
           (old_fflag.get('feature_version_id') != new_fflag.get('feature_version_id')):
              return update
        cls._ff_dict['feature_flags'].setdefault(version, {})
        cls._ff_dict['feature_flags'][version][feature_id] = new_fflag
        if old_fflag.get('enable_feature') != new_fflag.get('enable_feature'):
           if execute_cbs: cls._execute_callbacks(feature_id)
           update = True
        return update
    # end feature_flag_update


    @classmethod
    def is_feature_flag_changed(cls, old_fflag, new_fflag):
        return old_fflag != new_fflag
    # end if_feature_flag_changed

    @classmethod
    def delete_feature_flag(cls, feature_id, version):
        # remove feature id from flags and callbacks
        cls._ff_dict['feature_flags'][version].pop(feature_id)
        cls._ff_dict['callbacks'].pop(feature_id)
    # end delete_feature_flag

#end FeatureFlagBase
