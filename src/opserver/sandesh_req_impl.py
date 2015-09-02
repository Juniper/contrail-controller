#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver Sandesh Request Implementation
#

from sandesh.redis.ttypes import RedisUveInfo, RedisUVERequest, RedisUVEResponse

class OpserverSandeshReqImpl(object):
    def __init__(self, opserver):
        self._opserver = opserver
        RedisUVERequest.handle_request = self.handle_redis_uve_info_req
    # end __init__

    def handle_redis_uve_info_req(self, req):
        redis_uve_info = RedisUveInfo()
        uve_server = self._opserver.get_uve_server()
        uve_server.fill_redis_uve_info(redis_uve_info)
        redis_uve_resp = RedisUVEResponse(redis_uve_info)
        redis_uve_resp.response(req.context())
    # end handle_redis_uve_info_req

# end class OpserverSandeshReqImpl
