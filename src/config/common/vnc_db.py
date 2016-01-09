#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of database model for contrail config daemons
"""
from exceptions import NoIdError
from vnc_api.gen.resource_client import *
from utils import obj_type_to_vnc_class

class DBBase(object):
    # This is the base class for all DB objects. All derived objects must
    # have a class member called _dict of dictionary type.
    # The init method of this class must be callled before using any functions

    _logger = None
    _cassandra = None
    _manager = None

    @classmethod
    def init(cls, manager, logger, cassandra):
        cls._logger = logger
        cls._cassandra = cassandra
        cls._manager = manager
    # end init

    class __metaclass__(type):

        def __iter__(cls):
            for i in cls._dict:
                yield i
        # end __iter__

        def values(cls):
            for i in cls._dict.values():
                yield i
        # end values

        def items(cls):
            for i in cls._dict.items():
                yield i
        # end items
    # end __metaclass__

    @classmethod
    def get(cls, key):
        if key in cls._dict:
            return cls._dict[key]
        return None
    # end get

    @classmethod
    def locate(cls, key, *args):
        if key not in cls._dict:
            try:
                cls._dict[key] = cls(key, *args)
            except NoIdError as e:
                cls._logger.debug(
                    "Exception %s while creating %s for %s",
                    e, cls.__name__, key)
                return None
        return cls._dict[key]
    # end locate

    @classmethod
    def delete(cls, key):
        if key in cls._dict:
            del cls._dict[key]
    # end delete

    def get_ref_uuid_from_dict(self, obj_dict, ref_name):
        if ref_name in obj_dict:
            return obj_dict[ref_name][0]['uuid']
        else:
            return None

    def add_ref(self, ref_type, ref):
        if hasattr(self, ref_type):
            setattr(self, ref_type, ref)
        elif hasattr(self, ref_type+'s'):
            ref_set = getattr(self, ref_type+'s')
            ref_set.add(ref)
    # end add_ref

    def delete_ref(self, ref_type, ref):
        if hasattr(self, ref_type) and getattr(self, ref_type) == ref:
            setattr(self, ref_type, None)
        elif hasattr(self, ref_type+'s'):
            ref_set = getattr(self, ref_type+'s')
            ref_set.discard(ref)
    # end delete_ref

    def add_to_parent(self, obj_dict):
        self.parent_type = obj_dict.get('parent_type')
        self.parent_id = obj_dict.get('parent_uuid')
        if not self.parent_type or not self.parent_id:
            return
        p_obj = self._OBJ_TYPE_MAP[self.parent_type].get(self.parent_id)
        if p_obj is not None:
            p_obj.add_ref(self.obj_type, self.uuid)
    # end

    def remove_from_parent(self):
        if not self.parent_type or not self.parent_id:
            return
        p_obj = self._OBJ_TYPE_MAP[self.parent_type].get(self.parent_id)
        if p_obj is not None:
            p_obj.delete_ref(self.obj_type, self.uuid)

    def update_single_ref(self, ref_type, obj):
        refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        if refs:
            try:
                new_id = refs[0]['uuid']
            except KeyError:
                fq_name = refs[0]['to']
                new_id = self._cassandra.fq_name_to_uuid(ref_type, fq_name)
        else:
            new_id = None
        old_id = getattr(self, ref_type, None)
        if old_id == new_id:
            return
        ref_obj = self._OBJ_TYPE_MAP[ref_type].get(old_id)
        if ref_obj is not None:
            ref_obj.delete_ref(self.obj_type, self.uuid)
        ref_obj = self._OBJ_TYPE_MAP[ref_type].get(new_id)
        if ref_obj is not None:
            ref_obj.add_ref(self.obj_type, self.uuid)
        setattr(self, ref_type, new_id)
    # end update_single_ref

    def set_children(self, ref_type, obj):
        refs = obj.get(ref_type+'s')
        new_refs = set()
        for ref in refs or []:
            try:
                new_id = ref['uuid']
            except KeyError:
                fq_name = ref['to']
                new_id = self._cassandra.fq_name_to_uuid(ref_type, fq_name)
            new_refs.add(new_id)
        setattr(self, ref_type+'s', new_refs)
    # end

    def update_multiple_refs(self, ref_type, obj):
        refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        new_refs = set()
        for ref in refs or []:
            try:
                new_id = ref['uuid']
            except KeyError:
                fq_name = ref['to']
                new_id = self._cassandra.fq_name_to_uuid(ref_type, fq_name)
            new_refs.add(new_id)
        old_refs = getattr(self, ref_type+'s')
        for ref_id in old_refs - new_refs:
            ref_obj = self._OBJ_TYPE_MAP[ref_type].get(ref_id)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.uuid)
        for ref_id in new_refs - old_refs:
            ref_obj = self._OBJ_TYPE_MAP[ref_type].get(ref_id)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.uuid)
        setattr(self, ref_type+'s', new_refs)
    # end update_multiple_refs

    def read_obj(self, uuid, obj_type=None):
        method_name = "_cassandra_%s_read" % (obj_type or self.obj_type)
        method = getattr(self._cassandra, method_name)
        ok, objs = method([uuid])
        if not ok:
            self._logger.error(
                'Cannot read %s %s, error %s' % (obj_type, uuid, objs))
            raise NoIdError('')
        return objs[0]
    # end read_obj

    def get_parent_uuid(self, obj):
        if 'parent_uuid' in obj:
            return obj['parent_uuid']
        else:
            parent_type = obj['parent_type'].replace('-', '_')
            parent_fq_name = obj['fq_name'][:-1]
            return self._cassandra.fq_name_to_uuid(parent_type, parent_fq_name)
    # end get_parent_uuid

    @classmethod
    def find_by_name_or_uuid(cls, name_or_uuid):
        obj = cls.get(name_or_uuid)
        if obj:
            return obj

        for obj in cls.values():
            if obj.name == name_or_uuid:
                return obj
        return None
    # end find_by_name_or_uuid

    @classmethod
    def reset(cls):
        cls._dict = {}
# end class DBBase

DBBase._OBJ_TYPE_MAP = {
}

