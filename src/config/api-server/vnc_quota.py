import copy
from gen.resource_xsd import *
from gen.resource_common import *
from gen.resource_server import *

class QuotaHelper(object):

    default_quota = {
        'defaults': -1
        }

    @classmethod
    def get_project_dict(cls, proj_uuid, db_conn):
        (ok, proj_dict) = db_conn.dbe_read('project', {'uuid': proj_uuid})
        return (ok, proj_dict)

    @classmethod
    def get_quota_limit(cls, proj_dict, obj_type):
        quota = proj_dict.get('quota') or cls.default_quota
        quota_type = obj_type.replace('-','_')
        return (quota.get(quota_type) or
                cls.default_quota.get(quota_type) or
                cls.default_quota['defaults'])

    @classmethod
    def check_quota_limit(cls, proj_dict, obj_type, quota_count):
        quota_limit = cls.get_quota_limit(proj_dict, obj_type)
        if quota_limit > 0 and quota_count >= quota_limit:
            return (False, 'quota limit (%d) exceeded for resource %s' % (quota_limit, obj_type))
        return (True, quota_limit)
