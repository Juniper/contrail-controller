#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver Sandesh Request Implementation
#

from sandesh.redis.ttypes import RedisUveInfo, RedisUVERequest, RedisUVEResponse
from sandesh.analytics.ttypes import DiskUsageSetRequest, \
     DiskUsageGetRequest, DiskUsageResponse

class OpserverSandeshReqImpl(object):
    def __init__(self, opserver):
        self._opserver = opserver
        RedisUVERequest.handle_request = self.handle_redis_uve_info_req
        DiskUsageSetRequest.handle_request = self.handle_disk_usage_set_req
        DiskUsageGetRequest.handle_request = self.handle_disk_usage_get_req
    # end __init__

    def handle_redis_uve_info_req(self, req):
        redis_uve_info = RedisUveInfo()
        uve_server = self._opserver.get_uve_server()
        uve_server.fill_redis_uve_info(redis_uve_info)
        redis_uve_resp = RedisUVEResponse(redis_uve_info)
        redis_uve_resp.response(req.context())
    # end handle_redis_uve_info_req

    def send_disk_usage_resp(self, context):
        resp = DiskUsageResponse(self._opserver.disk_usage)
        resp.response(context)
    # end send_disk_usage_resp

    def handle_disk_usage_set_req(self, req):
        self._opserver.handle_disk_usage(req.disk_usage)
        self.send_disk_usage_resp(req.context())
    # end handle_disk_usage_set_req

    def handle_disk_usage_get_req(self, req):
        self.send_disk_usage_resp(req.context())
    # end handle_disk_usage_get_req

# end class OpserverSandeshReqImpl
