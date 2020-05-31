from kube_manager.vnc.config_db import DBBaseKM


class DBMock(object):
    """
    Mock for KubeNetworkManagerDB class
    """

    @staticmethod
    def init():
        DBMock.db = {}
        for cls in list(DBBaseKM.get_obj_type_map().values()):
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
        return DBMock._list(obj_type)

    @staticmethod
    def _list(obj_type):
        if obj_type not in DBMock.db:
            return False, None, None
        ret = [(val["fq_name"], val["uuid"]) for val in list(DBMock.db[obj_type].values())]
        return True, ret, None

    @staticmethod
    def get_dict(obj_type):
        return DBMock.db.get(obj_type, None)
