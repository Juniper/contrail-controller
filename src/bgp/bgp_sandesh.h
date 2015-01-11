/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_SANDESH_H_
#define BGP_SANDESH_H_

#include <boost/function.hpp>
#include <sandesh/sandesh.h>

#include "xmpp/xmpp_sandesh.h"

class BgpServer;
class BgpXmppChannelManager;
class IFMapServer;

class BgpNeighborResp;
class BgpNeighborReq;
class ShowNeighborStatisticsReq;

struct BgpSandeshContext : public XmppSandeshContext {
    typedef boost::function<void(std::vector<BgpNeighborResp> *,
                                 BgpSandeshContext *,
                                 const BgpNeighborReq *)>
            NeighborListExtension;
    typedef boost::function<void(size_t *,
                                 BgpSandeshContext *,
                                 const ShowNeighborStatisticsReq *)>
            NeighborStatisticsExtension;

    BgpSandeshContext();

    void SetNeighborShowExtensions(
        const NeighborListExtension &show_neighbor,
        const NeighborListExtension &show_neighbor_summary,
        const NeighborStatisticsExtension &show_neighbor_statistics);

    BgpServer *bgp_server;
    BgpXmppChannelManager *xmpp_peer_manager;
    IFMapServer *ifmap_server;

    void ShowNeighborExtension(std::vector<BgpNeighborResp> *list,
                               const BgpNeighborReq *req);
    void ShowNeighborSummaryExtension(std::vector<BgpNeighborResp> *list,
                                      const BgpNeighborReq *req);
    void ShowNeighborStatisticsExtension(size_t *count,
                                         const ShowNeighborStatisticsReq *req);

private:
    NeighborListExtension show_neighbor_ext_;
    NeighborListExtension show_neighbor_summary_ext_;
    NeighborStatisticsExtension show_neighbor_statistics_ext_;
};

#endif /* BGP_SANDESH_H_ */
