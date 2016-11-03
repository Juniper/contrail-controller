#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""
This is RDBMS backend module responsible for O/R mapping with RDBMS.
"""


import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import logging
import logging.config
import cfgm_common
from cfgm_common import jsonutils as json

logger = logging.getLogger(__name__)

CONFIG_VERSION = '1.0'

from cfgm_common.vnc_rdbms import VncRDBMSClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class VncServerRDBMSClient(VncRDBMSClient):
    def __init__(self, db_client_mgr, *args, **kwargs):
        self._db_client_mgr = db_client_mgr
        self._subnet_path = "/api-server/subnets"
        super(VncServerRDBMSClient, self).__init__(*args, **kwargs)

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)

    def enable_domain_sharing(self, obj_uuid, perms2):
        share_item = {
            'tenant': 'domain:%s' % obj_uuid,
            'tenant_access': cfgm_common.DOMAIN_SHARING_PERMS
        }
        perms2['share'].append(share_item)
        res_type = self.uuid_to_obj_type(obj_uuid)
        self.object_update(res_type, obj_uuid, {"perms2": perms2})

    def walk(self, fn):
        walk_results = []
        type_to_sqa_objs_info = self.get_all_sqa_objs_info()
        for obj_type in type_to_sqa_objs_info:
            for sqa_obj_info in type_to_sqa_objs_info[obj_type]:
                self.cache_uuid_to_fq_name_add(
                    sqa_obj_info[0], #uuid
                    json.loads(sqa_obj_info[1]), #fqname
                    obj_type)

        for obj_type in type_to_sqa_objs_info:
            uuid_list = [o[0] for o in type_to_sqa_objs_info[obj_type]]
            try:
                self.config_log('Resync: obj_type %s len %s'
                                %(obj_type, len(uuid_list)),
                                level=SandeshLevel.SYS_INFO)
                result = fn(obj_type, uuid_list)
                if result:
                    walk_results.append(result)
            except Exception as e:
                self.config_log('Error in db walk invoke %s' %(str(e)),
                                level=SandeshLevel.SYS_ERR)
                continue

        return walk_results