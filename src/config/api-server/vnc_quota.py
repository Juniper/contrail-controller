import copy
from gen.resource_xsd import *
from gen.resource_common import *
from gen.resource_server import *

class QuotaHelper(object):

    default_quota = {
        'default': -1
        }

    @staticmethod
    def ensure_quota_project_present(obj_type, proj_uuid, proj_dict, db_conn):
        quota = QuotaType()
        if 'quota' in proj_dict.keys():
            quota = QuotaType(**proj_dict['quota'])
        quota.set_defaults(QuotaHelper.default_quota['default'])

        set_quota_method = 'set_' + obj_type.replace('-','_')
        get_quota_method = 'get_' + obj_type.replace('-','_')
        if hasattr(quota, set_quota_method):
            get_quota = getattr(quota, get_quota_method)
            if get_quota() == None:
                set_quota = getattr(quota, set_quota_method)
                if obj_type in QuotaHelper.default_quota.keys():
                    set_quota(QuotaHelper.default_quota[obj_type])
                else:
                    set_quota(quota.get_defaults())

        proj_dict['quota'] = quota.__dict__
        (ok, result) = db_conn.dbe_update('project', {'uuid': proj_dict['uuid']}, proj_dict)
        if not ok:
            return (False, 'Cannot update project object')
        return (True, proj_dict)

    @staticmethod
    def get_objtype_dict(proj_uuid, obj_type, db_conn):
        proj_id = {'uuid': proj_uuid}
        (ok, proj_dict) = db_conn.dbe_read(obj_type, proj_id)
        if not ok:
            return (False, obj_type + ' is not valid')
        return (ok, proj_dict)

    @staticmethod
    def get_project_dict(proj_uuid, db_conn):
        return QuotaHelper.get_objtype_dict(proj_uuid, 'project', db_conn)

    @staticmethod
    def get_quota_limit(proj_dict, obj_type):
        quota = proj_dict['quota']
        quota_type = obj_type.replace('-','_')
        if quota_type in proj_dict['quota'].keys() and proj_dict['quota'][quota_type]:
            return proj_dict['quota'][quota_type]
        return QuotaHelper.default_quota['default']

    @staticmethod
    def check_quota_limit(proj_dict, obj_type, quota_count):
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
        if quota_limit > 0 and quota_count >= quota_limit:
            return (False, 'quota limit (%d) exceeded for resource %s' % (quota_limit, obj_type))
        return (True, quota_limit)

