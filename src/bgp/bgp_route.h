/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_ROUTE_H_
#define SRC_BGP_BGP_ROUTE_H_

#include <string>
#include <vector>

#include "bgp/bgp_path.h"
#include "base/address.h"
#include "route/route.h"

class BgpAttr;
struct BgpProtoPrefix;
class IPeer;
class BgpTable;
class ShowRoute;
class ShowRouteBrief;

class BgpRoute : public Route {
public:
    BgpRoute();
    ~BgpRoute();

    bool HasPaths() const { return front() != NULL; }
    const BgpPath *BestPath() const;

    void InsertPath(BgpPath *path);
    void DeletePath(BgpPath *path);

    const BgpPath *FindPath(BgpPath::PathSource src) const;
    const BgpPath *FindPath(const IPeer *peer,
                            bool include_secondary = false) const;
    BgpPath *FindPath(const IPeer *peer, bool include_secondary = false);
    BgpPath *FindPath(const IpAddress &nexthop);
    BgpPath *FindPath(BgpPath::PathSource src, const IPeer *peer,
                      uint32_t path_id);
    BgpPath *FindPath(BgpPath::PathSource src, uint32_t path_id);
    bool RemovePath(BgpPath::PathSource src, const IPeer *peer = NULL,
                    uint32_t path_id = 0);
    bool RemovePath(BgpPath::PathSource src, uint32_t path_id);
    bool RemovePath(const IPeer *peer);

    bool IsUsable() const;
    virtual bool IsValid() const;

    // Check if there's a better path with the same forwarding information.
    bool DuplicateForwardingPath(const BgpPath *in_path) const;

    BgpPath *FindSecondaryPath(BgpRoute *src_rt, BgpPath::PathSource src,
            const IPeer *peer, uint32_t path_id);
    bool RemoveSecondaryPath(const BgpRoute *src_rt, BgpPath::PathSource src,
            const IPeer *peer, uint32_t path_id);
    virtual RouteDistinguisher GetRouteDistinguisher() const {
        return RouteDistinguisher::kZeroRd;
    }

    void NotifyOrDelete();
    BgpTable *table();
    const BgpTable *table() const;

    virtual std::string ToXmppIdString() const { return ToString(); }

    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  const BgpAttr *attr = NULL,
                                  uint32_t label = 0,
                                  uint32_t l3_label = 0) const {
    }
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                      IpAddress nexthop) const {
    }

    // number of paths
    size_t count() const;

    // Fill info needed for introspect
    void FillRouteInfo(const BgpTable *table, ShowRouteBrief *show_route) const;
    void FillRouteInfo(const BgpTable *table, ShowRoute *show_route,
        const std::string &source = "", const std::string &protocol = "") const;
    uint32_t SubClusterId() const;
    void AddExtCommunitySubCluster(BgpPath *path);

private:
    DISALLOW_COPY_AND_ASSIGN(BgpRoute);
};

#endif  // SRC_BGP_BGP_ROUTE_H_
