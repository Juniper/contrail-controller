from gen.resource_xsd import *
from gen.resource_common import *
from pprint import pformat
import cfgm_common.exceptions

QUOTA_OVER_ERROR_CODE = 412

class QuotaHelper(object):

    default_quota = {
        'defaults': -1
        }

    @classmethod
    def get_project_dict_for_quota(cls, proj_uuid, db_conn):
        try:
            (ok, proj_dict) = db_conn.dbe_read('project', {'uuid': proj_uuid},
                                           obj_fields=['quota'])
        except cfgm_common.exceptions.NoIdError as e:
            return (False, str(e))

        return (ok, proj_dict)

    @classmethod
    def get_quota_limit(cls, proj_dict, obj_type):
        quota = proj_dict.get('quota') or cls.default_quota
        quota_limit = quota.get(obj_type)
        if quota_limit is None:
            quota_limit = cls.default_quota.get(obj_type)
        if quota_limit is None:
            quota_limit = cls.default_quota['defaults']
        return quota_limit

    @classmethod
    def check_quota_limit(cls, proj_dict, obj_type, quota_count):
        quota_limit = cls.get_quota_limit(proj_dict, obj_type)
        if quota_limit >= 0 and quota_count >= quota_limit:
            return (False, 'quota limit (%d) exceeded for resource %s'
                    % (quota_limit, obj_type))
        return (True, quota_limit)

    @classmethod
    def verify_quota_for_resource(cls, db_conn, resource, obj_type,
                                  user_visibility, proj_uuid=None, fq_name=[]):
        if not user_visibility:
            return True, ""

        if not proj_uuid and fq_name:
            try:
                proj_uuid = db_conn.fq_name_to_uuid('project', fq_name[0:2])
            except cfgm_common.exceptions.NoIdError:
                return (False, (500, 'No Project ID error : ' + proj_uuid))

        (ok, proj_dict) = cls.get_project_dict_for_quota(proj_uuid, db_conn)

        if not ok:
            return (False, (500, 'Internal error : ' + pformat(proj_dict)))

        quota_limit = cls.get_quota_limit(proj_dict, obj_type)

        # Quota limit is not enabled for this resource
        if quota_limit < 0:
            return True, ""

        if resource in Project.children_fields:
            # Number of resources created under this project.
            # Resouce is a child ref under project object
            (ok, quota_count) = db_conn.dbe_count_children(
                'project', proj_uuid, resource)
            if not ok:
                return (False, (500, 'Internal error : Failed to read current '
                                'resource count'))
        else:
            (ok, res_list) = db_conn.dbe_list(obj_type,
                                              back_ref_uuids=[proj_uuid])
            if not ok:
                return (False, (500, 'Internal error : Failed to read %s '
                                'resource list' % obj_type))
            quota_count = len(res_list)

        if quota_count >= quota_limit:
            msg = ('quota limit (%d) exceeded for resource %s'
                   % (quota_limit, obj_type))
            return (False, (QUOTA_OVER_ERROR_CODE, pformat(fq_name) + ' : ' + msg))

        return True, ""
