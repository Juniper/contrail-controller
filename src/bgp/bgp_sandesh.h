/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_SANDESH_H_
#define BGP_SANDESH_H_

#include <boost/function.hpp>
#include <sandesh/sandesh.h>

class BgpServer;
class BgpXmppChannelManager;
class BgpNeighborResp;
class BgpNeighborReq;
class ShowBgpNeighborSummaryReq;
class ShowNeighborStatisticsReq;

struct BgpSandeshContext : public SandeshContext {
    typedef boost::function<bool(const BgpSandeshContext *, bool,
        uint32_t, uint32_t, const std::string &, const std::string &,
        std::vector<BgpNeighborResp> *, std::string *)> NeighborListExtension;

    typedef boost::function<void(size_t *, const BgpSandeshContext *,
        const ShowNeighborStatisticsReq *)> NeighborStatisticsExtension;

    BgpSandeshContext();

    void SetNeighborShowExtensions(
        const NeighborListExtension &show_neighbor,
        const NeighborStatisticsExtension &show_neighbor_statistics);

    BgpServer *bgp_server;
    BgpXmppChannelManager *xmpp_peer_manager;

    bool ShowNeighborExtension(const BgpSandeshContext *bsc, bool summary,
        uint32_t page_limit, uint32_t iter_limit,
        const std::string &start_neighbor, const std::string &search_string,
        std::vector<BgpNeighborResp> *list, std::string *next_neighbor) const;
    void ShowNeighborStatisticsExtension(size_t *count,
        const ShowNeighborStatisticsReq *req) const;

    // For testing.
    bool test_mode() const { return test_mode_; }
    void set_test_mode(bool test_mode) { test_mode_ = test_mode; }
    uint32_t page_limit() const { return page_limit_; }
    void set_page_limit(uint32_t page_limit) { page_limit_ = page_limit; }
    uint32_t iter_limit() const { return iter_limit_; }
    void set_iter_limit(uint32_t iter_limit) { iter_limit_ = iter_limit; }

private:
    bool test_mode_;
    uint32_t page_limit_;
    uint32_t iter_limit_;
    NeighborListExtension show_neighbor_ext_;
    NeighborStatisticsExtension show_neighbor_statistics_ext_;
};

#endif /* BGP_SANDESH_H_ */
