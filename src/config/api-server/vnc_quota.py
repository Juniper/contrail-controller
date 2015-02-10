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
        quota_limit = quota.get(quota_type)
        if quota_limit is None:
            quota_limit = cls.default_quota.get(quota_type)
        if quota_limit is None:
            quota_limit = cls.default_quota['defaults']
        return quota_limit

    @classmethod
    def check_quota_limit(cls, proj_dict, obj_type, quota_count):
        quota_limit = cls.get_quota_limit(proj_dict, obj_type)
        if quota_limit > 0 and quota_count >= quota_limit:
            return (False, 'quota limit (%d) exceeded for resource %s' % (quota_limit, obj_type))
        return (True, quota_limit)

    @classmethod
    def verify_quota_for_resource(cls, db_conn, resource, obj_type,
                                  user_visibility, proj_uuid=None, fq_name=[]):
        if not proj_uuid and fq_name:
            try:
                proj_uuid = db_conn.fq_name_to_uuid('project', fq_name[0:2])
            except cfgm_common.exceptions.NoIdError:
                return (False, (500, 'No Project ID error : ' + proj_uuid))

        (ok, proj_dict) = cls.get_project_dict(proj_uuid, db_conn)
        if not ok:
            return (False, (500, 'Internal error : ' + pformat(proj_dict)))

        if not user_visibility:
            return True, ""

        quota_count = len(proj_dict.get(resource, []))
        (ok, quota_limit) = cls.check_quota_limit(proj_dict, obj_type,
                                                  quota_count)
        if not ok:
            return (False, (403, pformat(fq_name) + ' : ' + quota_limit))
        return True, ""

