from gen.resource_xsd import *
from gen.resource_common import *
from gen.resource_server import *
from pprint import pformat


class QuotaHelper(object):

    default_quota = {
        'defaults': -1
        }

    @classmethod
    def get_project_dict(cls, proj_uuid, db_conn, obj_fields=['quota']):
        (ok, proj_dict) = db_conn.dbe_read('project', {'uuid': proj_uuid},
                                           obj_fields=obj_fields)
        return (ok, proj_dict)

    @classmethod
    def get_quota_limit(cls, proj_dict, obj_type):
        quota = proj_dict.get('quota') or cls.default_quota
        quota_type = obj_type.replace('-', '_')
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
            return (False, 'quota limit (%d) exceeded for resource %s'
                    % (quota_limit, obj_type))
        return (True, quota_limit)

    @classmethod
    def verify_quota_for_resource(cls, db_conn, resource, obj_type,
                                  user_visibility, proj_uuid=None, fq_name=[],
                                  child_refs=True):
        if not user_visibility:
            return True, ""

        if not proj_uuid and fq_name:
            try:
                proj_uuid = db_conn.fq_name_to_uuid('project', fq_name[0:2])
            except cfgm_common.exceptions.NoIdError:
                return (False, (500, 'No Project ID error : ' + proj_uuid))

        if child_refs:
            (ok, proj_dict) = cls.get_project_dict(proj_uuid, db_conn)
        else:
            (ok, proj_dict) = cls.get_project_dict(
                proj_uuid, db_conn, obj_fields=['quota', resource])

        if not ok:
            return (False, (500, 'Internal error : ' + pformat(proj_dict)))

        quota_limit = cls.get_quota_limit(proj_dict, obj_type)

        # Quota limit is not enabled for this resource
        if quota_limit < 0:
            return True, ""

        if not child_refs:
            # old way if the resource is not child of Project
            quota_count = len(proj_dict.get(resource, []))
            if quota_count >= quota_limit:
                msg = ('quota limit (%d) exceeded for resource %s'
                       % (quota_limit, obj_type))
                return (False, (403, pformat(fq_name) + ' : ' + msg))
            return True, ''

        # Number of resources created under this project.
        # Resouce is a child ref under project object
        (ok, quota_count) = db_conn.dbe_count_children('project', proj_uuid,
                                                       resource)
        if not ok:
            return (False, (500, 'Internal error : Failed to read current '
                            'resource count'))

        if quota_count >= quota_limit:
            msg = ('quota limit (%d) exceeded for resource %s'
                   % (quota_limit, obj_type))
            return (False, (403, pformat(fq_name) + ' : ' + msg))

        return True, ""
