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
            quota = QuotaType(proj_dict['quota'])
        quota.set_defaults(QuotaHelper.default_quota['default'])

        quota_method = 'set_' + obj_type.replace('-','_')
        if hasattr(quota, quota_method):
            set_quota = getattr(quota, quota_method)
            if obj_type in QuotaHelper.default_quota.keys():
                set_quota(default_quota[obj_type])
            else:
                set_quota(quota.defaults)

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
            return (False, objtype + ' is not valid')
        return (ok, proj_dict)

    @staticmethod
    def get_project_dict(proj_uuid, db_conn):
        return QuotaHelper.get_objtype_dict(proj_uuid, 'project', db_conn)

    @staticmethod
    def get_quota_limit(proj_dict, obj_type):
        quota = proj_dict['quota']
        quota_method = obj_type.replace('-','_')
        if hasattr(quota, quota_method):
            get_quota = getattr(quota, quota_method)
            return get_quota()
        return QuotaHelper.default_quota['default']

    @staticmethod
    def check_quota_limit(proj_dict, obj_type, quota_count):
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
        if quota_limit > 0 and quota_count >= quota_limit:
            return (False, 'quota limit (%d) exceeded for resource %s' % (quota_limit, obj_type))
        return (True, quota_limit)

