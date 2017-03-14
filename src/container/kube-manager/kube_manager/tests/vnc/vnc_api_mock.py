import mock
import unittest
import functools
import types
from mock import patch, Mock

from vnc_api.vnc_api import *
from db_mock import *

# Mock for VncApi class

class VncApiMock(object):

    def __init__(self, admin_user, admin_password, admin_tenant,
                 vnc_endpoint_ip, vnc_endpoint_port, auth_token_url):

        for object_type, resource_type in all_resource_type_tuples:
            for oper_str in ('_create', '_read', '_update', '_delete', 's_list', '_get_default_id'):
                method = getattr(self, '_object%s' %(oper_str))
                bound_method = functools.partial(method, resource_type)
                functools.update_wrapper(bound_method, method)
                if oper_str == '_get_default_id':
                    setattr(self, 'get_default_%s_id' % (object_type),
                            bound_method)
                else:
                    setattr(self, '%s%s' %(object_type, oper_str),
                            bound_method)

    @staticmethod
    def _alloc_uuid():
        uuid = VncApiMock.next_uuid
        VncApiMock.next_uuid += 1
        return str(uuid)

    @staticmethod
    def init():
        DBMock.init()
        VncApiMock.name_to_uuid = {}
        for object_type, resource_type in all_resource_type_tuples:
            VncApiMock.name_to_uuid[resource_type] = {}
        VncApiMock.next_uuid = 0

    def _object_create(self, res_type, obj):
        return VncApiMock.create(res_type, obj)

    @staticmethod
    def create(res_type, obj):
        if obj.name in VncApiMock.name_to_uuid.get(res_type):
            raise RefsExistError()
        obj.uuid = VncApiMock._alloc_uuid()

        DBMock.create(res_type.replace('-', '_'), obj.uuid, VncApiMock.object_to_dict(obj))
        VncApiMock.name_to_uuid.get(res_type)[obj.name] = obj.uuid
        return obj.uuid

    def _object_read(self, res_type, fq_name=None, fq_name_str=None, id=None, fields=None):
        return VncApiMock.read(res_type, fq_name, fq_name_str, id, fields)

    @staticmethod
    def read(res_type, fq_name=None, fq_name_str=None, id=None, fields=None):
        (args_ok, result) = VncApiMock._read_args_to_id(res_type, fq_name, fq_name_str, id)
        if not args_ok:
            return result
        id = result
        if id is None:
            raise NoIdError("Object does not exist.")
        ok, ret = DBMock.read(res_type.replace('-', '_'), [id])
        if (not ok) or (len(ret) < 1):
            raise NoIdError("Object does not exist.")
        return VncApiMock.object_from_dict(res_type, ret[0])

    def _object_update(self, res_type, obj):
        return VncApiMock._object_update(res_type, obj)

    @staticmethod
    def update(res_type, obj):
        if not obj.uuid:
            obj.uuid = VncApiMock.name_to_id(res_type, obj.get_name())
        if obj.uuid is None:
            raise NoIdError("Object does not exist.")
        if obj.uuid not in DBMock.get_dict(res_type.replace('-', '_')):
            raise NoIdError("Object does not exist.")
        DBMock.update(res_type.replace('-', '_'), obj.uuid, VncApiMock.object_to_dict(obj))

    def _objects_list(self, res_type, parent_id=None, parent_fq_name=None,
                      obj_uuids=None, back_ref_id=None, fields=None,
                      detail=False, count=False, filters=None, shared=False):
        return VncApiMock.list(res_type, parent_id, parent_fq_name,
                               obj_uuids, back_ref_id, fields, detail, count, filters, shared)

    @staticmethod
    def list(res_type, parent_id=None, parent_fq_name=None,
             obj_uuids=None, back_ref_id=None, fields=None,
             detail=False, count=False, filters=None, shared=False):
        ret = []
        for obj_dict in DBMock.get_dict(res_type.replace('-', '_')).values():
            obj = VncApiMock.object_from_dict(res_type, obj_dict)
            if parent_id is not None and parent_id != VncApiMock.name_to_uuid(obj.parent_name()):
                continue
            if parent_fq_name is not None and parent_fq_name != obj.get_parent_fq_name():
                continue
            if obj_uuids is not None and obj.uuid not in obj_uuids:
                continue
            ret.append(obj)
        return ret

    def _object_delete(self, res_type, fq_name=None, id=None, ifmap_id=None):
        return VncApiMock.delete(res_type, fq_name, id, ifmap_id)

    @staticmethod
    def delete(res_type, fq_name=None, id=None, ifmap_id=None):
        (args_ok, result) = VncApiMock._read_args_to_id(res_type, fq_name=fq_name, id=id)
        if not args_ok:
            return result
        id = result
        if id is None:
            raise NoIdError("Object does not exist.")
        obj = DBMock.read(res_type.replace('-', '_'), [id])
        VncApiMock.name_to_uuid.pop(obj.get_name())
        DBMock.delete(res_type.replace('-', '_'), id)

    def _object_get_default_id(self, res_type):
        return 0

    @staticmethod
    def _read_args_to_id(res_type, fq_name=None, fq_name_str=None, id=None):
        arg_count = ((fq_name is not None) + (fq_name_str is not None) + (id is not None))

        if (arg_count == 0):
            return (False, "at least one of the arguments has to be provided")
        elif (arg_count > 1):
            return (False, "only one of the arguments should be provided")

        if id:
            return (True, id)
        if fq_name:
            return (True, VncApiMock.name_to_id(res_type, fq_name[-1]))
        if fq_name_str:
            return (True, VncApiMock.name_to_id(res_type, fq_name_str.split(':')[-1]))

    @staticmethod
    def name_to_id(res_type, name):
        return VncApiMock.name_to_uuid.get(res_type).get(name)

    def chown(self, obj_uuid, owner):
        pass

    @staticmethod
    def object_from_dict(res_type, obj_dict):
        obj_cls = get_object_class(res_type)
        return obj_cls.from_dict(**copy.deepcopy(obj_dict))

    @staticmethod
    def object_to_dict(obj):
        return json.loads(json.dumps(obj, default=VncApiMock._obj_serializer_all), encoding="ascii")

    @staticmethod
    def _obj_serializer_all(obj):
        if hasattr(obj, 'serialize_to_json'):
            return obj.serialize_to_json()
        else:
            return dict((k, v) for k, v in obj.__dict__.iteritems())

    @staticmethod
    def is_jsonable(x):
        try:
            json.dumps(x)
            return True
        except TypeError:
            return False
