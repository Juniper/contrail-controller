import mock
import unittest
from mock import patch, Mock

from kube_manager.vnc.config_db import DBBaseKM

# Mock for KubeNetworkManagerDB class

class DBMock(object):

    @staticmethod
    def init():
        DBMock.db = {}
        for cls in DBBaseKM.get_obj_type_map().values():
            DBMock.db[cls.obj_type] = {}

    def __init__(self, args, logger):
        pass

    def object_create(self, obj_type, uuid, obj):
        DBMock.create(obj_type, uuid, obj)

    @staticmethod
    def create(obj_type, uuid, obj):
        DBMock.db[obj_type][uuid] = obj

    @staticmethod
    def update(obj_type, uuid, obj):
        DBMock.create(obj_type, uuid, obj)

    @staticmethod
    def delete(obj_type, uuid):
        DBMock.db.get(obj_type).pop(uuid)

    def object_read(self, obj_type, uuid_list, field_names=None):
        return DBMock.read(obj_type, uuid_list, field_names)

    @staticmethod
    def read(obj_type, uuid_list, field_names=None):
        ret = []
        for uuid in uuid_list:
            if obj_type not in DBMock.db or uuid not in DBMock.db.get(obj_type):
                return False, None
            ret.append(DBMock.db.get(obj_type).get(uuid))
        return True, ret

    def object_list(self, obj_type):
        return DBMock.list(obj_type)

    @staticmethod
    def list(obj_type):
        if obj_type not in DBMock.db:
            return False, None
        ret = [(val["fq_name"], val["uuid"]) for val in DBMock.db[obj_type].values()]
        return True, ret

    @staticmethod
    def get_dict(obj_type):
        return DBMock.get(obj_type, None)
