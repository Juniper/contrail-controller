/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_ifmap_sandesh.h"

#include "bgp/bgp_sandesh.h"

class ShowBgpPeeringConfigReq;
class ShowBgpPeeringConfigReqIterate;

extern void ShowBgpIfmapPeeringConfigReqHandler(
    const BgpSandeshContext *bsc,
    const ShowBgpPeeringConfigReq *req);
extern void ShowBgpIfmapPeeringConfigReqIterateHandler(
    const BgpSandeshContext *bsc,
    const ShowBgpPeeringConfigReqIterate *req_iterate);

void RegisterSandeshShowIfmapHandlers(BgpSandeshContext *bsc) {
    bsc->SetPeeringShowHandlers(
        ShowBgpIfmapPeeringConfigReqHandler,
        ShowBgpIfmapPeeringConfigReqIterateHandler);
}
