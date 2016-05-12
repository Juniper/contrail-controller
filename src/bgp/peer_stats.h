/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_PEER_STATS_H_
#define SRC_BGP_PEER_STATS_H_

#include "sandesh/sandesh.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/ipeer.h"

class PeerStats {
public:
    static void FillPeerDebugStats(const IPeerDebugStats *peer_state,
                                   PeerStatsInfo *stats);

private:
    static void FillProtoStats(const IPeerDebugStats::ProtoStats &stats,
                               PeerProtoStats *proto_stats);
    static void FillRouteUpdateStats(const IPeerDebugStats::UpdateStats &stats,
    PeerUpdateStats *rt_stats);
    static void FillRxErrorStats(const IPeerDebugStats::RxErrorStats &src,
                                 PeerRxErrorStats *dest);
    static void FillRxRouteStats(const IPeerDebugStats::RxRouteStats &src,
                                 PeerRxRouteStats *dest);
};

#endif  // SRC_BGP_PEER_STATS_H_
