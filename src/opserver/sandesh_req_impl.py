#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver Sandesh Request Implementation
#

from sandesh.redis.ttypes import RedisUveInfo, RedisUVERequest, RedisUVEResponse
from sandesh.analytics.ttypes import DbInfoSetRequest, \
     DbInfoGetRequest, DbInfoResponse, DbInfo

class OpserverSandeshReqImpl(object):
    def __init__(self, opserver):
        self._opserver = opserver
        RedisUVERequest.handle_request = self.handle_redis_uve_info_req
        DbInfoSetRequest.handle_request = self.handle_db_info_set_req
        DbInfoGetRequest.handle_request = self.handle_db_info_get_req
    # end __init__

    def handle_redis_uve_info_req(self, req):
        redis_uve_info = RedisUveInfo()
        uve_server = self._opserver.get_uve_server()
        uve_server.fill_redis_uve_info(redis_uve_info)
        redis_uve_resp = RedisUVEResponse(redis_uve_info)
        redis_uve_resp.response(req.context())
    # end handle_redis_uve_info_req

    def send_db_info_resp(self, context):
        db_info = DbInfo(self._opserver.disk_usage_percentage,
                         self._opserver.pending_compaction_tasks,
                         0, 0)
        resp = DbInfoResponse(db_info)
        resp.response(context)
    # end send_db_info_resp

    def handle_db_info_set_req(self, req):
        self._opserver.handle_db_info(req.disk_usage_percentage,
                                      req.pending_compaction_tasks)
        self.send_db_info_resp(req.context())
    # end handle_db_info_set_req

    def handle_db_info_get_req(self, req):
        self.send_db_info_resp(req.context())
    # end handle_db_info_get_req

# end class OpserverSandeshReqImpl
