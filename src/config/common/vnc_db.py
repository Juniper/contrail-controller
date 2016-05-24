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

    # objects in the database could be indexed by uuid or fq-name
    # set _indexed_by_name to True in the derived class to use fq-name as index
    _indexed_by_name = False

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

        def __contains__(cls, item):
            # check for 'item in cls'
            return item in cls._dict
        # end __contains__
    # end __metaclass__

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
        obj = cls.get(key)
        if obj is None:
            return
        obj.delete_obj()
        del cls._dict[key]
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
                fq_name = obj_dict.get('fq_name', [])
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
                self.parent_key = obj.get_parent_uuid
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
                key = self._cassandra.fq_name_to_uuid(ref_type, fq_name)
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
            return
        ref_obj = self.get_obj_type_map()[ref_type].get(old_key)
        if ref_obj is not None:
            ref_obj.delete_ref(self.obj_type, self.get_key())
        ref_obj = self.get_obj_type_map()[ref_type].get(new_key)
        if ref_obj is not None:
            ref_obj.add_ref(self.obj_type, self.get_key())
        setattr(self, ref_type, new_key)
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
        for ref_key in old_refs - new_refs:
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.get_key())
        for ref_key in new_refs - old_refs:
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.get_key())
        setattr(self, ref_type+'s', new_refs)
    # end update_multiple_refs

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
        for ref_key in set(old_refs.keys()) - set(new_refs.keys()):
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.get_key())
        for ref_key in new_refs:
            if ref_key in old_refs and new_refs[ref_key] == old_refs[ref_key]:
                continue
            ref_obj = self.get_obj_type_map()[ref_type].get(ref_key)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.get_key(), new_refs[ref_key])
        setattr(self, ref_type+'s', new_refs)
    # end update_multiple_refs

    @classmethod
    def read_obj(cls, uuid, obj_type=None):
        ok, objs = cls._cassandra.object_read(obj_type or cls.obj_type, [uuid])
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
    def read_vnc_obj(cls, uuid=None, fq_name=None, obj_type=None):
        if uuid is None and fq_name is None:
            raise NoIdError('')
        obj_type = obj_type or cls.obj_type
        if uuid is None:
            if isinstance(fq_name, basestring):
                fq_name = fq_name.split(':')
            uuid = cls._cassandra.fq_name_to_uuid(obj_type, fq_name)
        obj_dict = cls.read_obj(uuid, obj_type)
        obj = cls.vnc_obj_from_dict(obj_type, obj_dict)
        obj.clear_pending_updates()
        return obj
    # end read_vnc_obj

    @classmethod
    def list_obj(cls, obj_type=None):
        obj_type = obj_type or cls.obj_type
        ok, result = cls._cassandra.object_list(obj_type)
        if not ok:
            return []
        uuids = [uuid for _, uuid in result]
        ok, objs = cls._cassandra.object_read(obj_type, uuids)
        if not ok:
            return []
        return objs

    @classmethod
    def list_vnc_obj(cls, obj_type=None):
        obj_type = obj_type or cls.obj_type
        vnc_cls = obj_type_to_vnc_class(obj_type, __name__)
        obj_dicts = cls.list_obj(obj_type)
        for obj_dict in obj_dicts:
            obj = vnc_cls.from_dict(**obj_dict)
            obj.clear_pending_updates()
            yield obj

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

    @classmethod
    def get_obj_type_map(cls):
        module_base = [x for x in DBBase.__subclasses__()
                       if cls.__module__ == x.obj_type]
        return dict((x.obj_type, x) for x in module_base[0].__subclasses__())

# end class DBBase

