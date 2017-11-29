from gen.resource_xsd import *
from gen.resource_common import *
from pprint import pformat
import cfgm_common.exceptions

QUOTA_OVER_ERROR_CODE = 412
NON_OBJECT_TYPES = ['security_group_rule']

class QuotaHelper(object):

    default_quota = {
        'defaults': -1
        }

    @classmethod
    def get_project_dict_for_quota(cls, proj_uuid, db_conn):
        try:
            (ok, proj_dict) = db_conn.dbe_read('project', proj_uuid,
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
    def verify_quota(cls, obj_type, quota_limit, quota_counter, count=1):
        quota_count = quota_counter.value
        if (quota_count < quota_limit or count < 0):
            try:
                quota_counter += count
            except cfgm_common.exceptions.OverQuota:
                msg = ('quota limit (%d) exceeded for resource %s'
                       % (quota_limit, obj_type))
                return False, (412, msg)
        else:
            msg = ('quota limit (%d) exceeded for resource %s'
                   % (quota_limit, obj_type))
            return False, (412, msg)
        return True, ""

    @classmethod
    def verify_quota_and_create_resource(cls, db_conn, obj_dict, obj_type, obj_id,
                                         quota_limit, quota_counter):
        ok, result = cls.verify_quota(obj_type, quota_limit, quota_counter)
        if ok:
            (ok, result) = db_conn.dbe_create(obj_type, obj_id,
                                                    obj_dict)
	    if not ok:
                # revert back quota count
                quota_counter -= 1
	        return ok, result
        else:
            return ok, result
        return (True, result)

    @classmethod
    def get_security_group_rule_count(cls, db_conn, proj_uuid):

        (ok, res_list, _) = db_conn.dbe_list(
                'security_group', parent_uuids=[proj_uuid], is_detail=True)
        if not ok:
            return (False, (500, 'Internal error : Failed to read '
                            'security_group resource list'))
        quota_count = 0
        for sg_dict in res_list:
            if sg_dict['id_perms'].get('user_visible', True) is not False:
                sge = sg_dict.get('security_group_entries') or {}
                quota_count += len(sge.get('policy_rule') or [])

        return True, quota_count

    @classmethod
    def get_resource_count(cls, db_conn, obj_type, proj_uuid=None):

        if obj_type+'s' in Project.children_fields:
            # Number of resources created under this project.
            # Resource is a child ref under project object
            (ok, quota_count) = db_conn.dbe_count_children(
                'project', proj_uuid, obj_type+'s')
            if not ok:
                return (False, (500, 'Internal error : Failed to read current '
                                'resource count'))
        elif obj_type == 'security_group_rule':
                ok, result = cls.get_security_group_rule_count(
                        db_conn, proj_uuid)
                if not ok:
                    return (ok, result)
                quota_count = result
        else:
            (ok, res_list, _) = db_conn.dbe_list(obj_type,
                                              back_ref_uuids=[proj_uuid])
            if not ok:
                return (False, (500, 'Internal error : Failed to read %s '
                                'resource list' % obj_type))
            quota_count = len(res_list)
        return quota_count

    @classmethod
    def update_zk_counter_helper(cls, path_prefix, quota_dict, proj_id,
                                 db_conn, quota_counter):
        new_quota_dict = {}
        for (obj_type, quota) in quota_dict.iteritems():
            path = path_prefix + "/" + obj_type
            if path in quota_counter:
                if quota == -1 and db_conn._zk_db._zk_client.exists(path):
                    db_conn._zk_db._zk_client.delete_node(path)
                    del quota_counter[path]
                else:
                    quota_counter[path].max_count = quota
            else:
                new_quota_dict[obj_type] = quota

        if new_quota_dict:
            cls._zk_quota_counter_init(path_prefix, new_quota_dict, proj_id,
                                       db_conn, quota_counter)

    @classmethod
    def _zk_quota_counter_update(cls, path_prefix, quota_dict, proj_id, db_conn,
                                 quota_counter):
        if quota_dict != None:
            cls.update_zk_counter_helper(path_prefix, quota_dict, proj_id,
                                         db_conn, quota_counter)

        elif quota_dict == None and quota_counter:
            for counter in quota_counter.values():
                if db_conn._zk_db.quota_counter_exists(counter.path):
                    db_conn._zk_db.delete_quota_counter(counter.path)
            quota_counter = {}

    @classmethod
    def _zk_quota_counter_init(cls, path_prefix, quota_dict, proj_id, db_conn,
                               quota_counter):

        for (obj_type, quota) in quota_dict.iteritems():
            path = path_prefix + "/" + obj_type
            if  obj_type != 'defaults' and (quota != -1 and quota != None):
                resource_count = cls.get_resource_count(db_conn,
                                                        obj_type, proj_id)
                quota_counter[path] = db_conn._zk_db.quota_counter(path,
                                                         max_count=quota,
                                                         default=resource_count)
