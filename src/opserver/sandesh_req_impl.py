#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver Sandesh Request Implementation
#

from sandesh.redis.ttypes import RedisUveMasterInfo, RedisUVEMasterRequest, RedisUVEMasterResponse

class OpserverSandeshReqImpl(object):
    def __init__(self, opserver):
        self._opserver = opserver
        RedisUVEMasterRequest.handle_request = self.handle_redis_uve_master_req
    # end __init__

    def handle_redis_uve_master_req(self, req):
        uve_master_info = RedisUveMasterInfo()
        uve_server = self._opserver.get_uve_server()
        uve_server.fill_redis_uve_master_info(uve_master_info)
        uve_master_resp = RedisUVEMasterResponse(uve_master_info)
        uve_master_resp.response(req.context())
    # end handle_redis_uve_master_req

# end class OpserverSandeshReqImpl
