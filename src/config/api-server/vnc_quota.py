import copy

class QuotaHelper(object):

    default_quota = {
        'default': -1
        }

    @staticmethod
    def ensure_quota_project_present(obj_type, proj_uuid, proj_dict, db_conn):
        new_quota = copy.deepcopy(QuotaHelper.default_quota)
        if (('quota' not in proj_dict.keys()) or
            (proj_dict['quota'] is None)):
            proj_dict['quota'] = new_quota

        if obj_type not in new_quota.keys() and 'default' in new_quota.keys():
            proj_dict['quota'][obj_type] = new_quota['default']

        if obj_type not in proj_dict['quota'].keys():
            proj_dict['quota'].update(new_quota)

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
        return proj_dict['quota'][obj_type]

    @staticmethod
    def check_quota_limit(proj_dict, obj_type, quota_count):
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
        if quota_limit > 0 and quota_count >= quota_limit:
            return (False, 'quota limit (%d) exceeded for resource %s' % (quota_limit, obj_type))
        return (True, quota_limit)

