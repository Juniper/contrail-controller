/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_route_h
#define ctrlplane_bgp_route_h

#include "route/route.h"
#include "net/address.h"
#include "bgp/bgp_path.h"

class BgpAttr;
struct BgpProtoPrefix;
class IPeer;
class BgpTable;
class ShowRoute;

class BgpRoute : public Route {
public:
    BgpRoute();
    ~BgpRoute();

    const BgpPath *BestPath() const;

    void InsertPath(BgpPath *path);
    void DeletePath(BgpPath *path);

    BgpPath *FindPath(const IPeer *peer, uint32_t path_id);
    BgpPath *FindPath(const IPeer *peer) {
        return FindPath(peer, 0);
    }
    BgpPath *FindPath(uint32_t path_id) {
        return FindPath(NULL, path_id);
    }

    BgpPath *FindPath(const IPeer *peer, uint32_t path_id, 
                      BgpPath::PathSource src);
    BgpPath *FindPath(const IPeer *peer, BgpPath::PathSource src) {
        return FindPath(peer, 0, src);
    }
    BgpPath *FindPath(uint32_t path_id, BgpPath::PathSource src) {
        return FindPath(NULL, path_id, src);
    }

    const BgpPath *FindPath(const IPeer *peer, uint32_t path_id) const;
    const BgpPath *FindPath(const IPeer *peer) const {
        return FindPath(peer, 0);
    }
    const BgpPath *FindPath(uint32_t path_id) const {
        return FindPath(NULL, path_id);
    }

    const BgpPath *FindPath(const IPeer *peer, uint32_t path_id, 
                      BgpPath::PathSource src) const;
    const BgpPath *FindPath(const IPeer *peer, BgpPath::PathSource src)  const {
        return FindPath(peer, 0, src);
    }
    const BgpPath *FindPath(uint32_t path_id, BgpPath::PathSource src) const {
        return FindPath(NULL, path_id, src);
    }

    bool RemovePath(const IPeer *peer, uint32_t path_id);
    bool RemovePath(const IPeer *peer) {
        return RemovePath(peer, 0);
    }
    bool RemovePath(uint32_t path_id) {
        return RemovePath(NULL, path_id);
    }

    bool RemovePath(const IPeer *peer, uint32_t path_id, BgpPath::PathSource src);
    bool RemovePath(const IPeer *peer, BgpPath::PathSource src) {
        return RemovePath(peer, 0, src);
    }
    bool RemovePath(uint32_t path_id, BgpPath::PathSource src) {
        return RemovePath(NULL, path_id, src);
    }

    // Check if there's a better path with the same forwarding information.
    bool DuplicateForwardingPath(const BgpPath *in_path) const;

    BgpPath *FindSecondaryPath(BgpRoute *src_rt,
            const IPeer *peer, uint32_t path_id, BgpPath::PathSource src);
    BgpPath *FindSecondaryPath(BgpRoute *src_rt, const IPeer *peer, 
                               BgpPath::PathSource src) {
        return FindSecondaryPath(src_rt, peer, 0, src);
    }
    BgpPath *FindSecondaryPath(BgpRoute *src_rt, uint32_t path_id, 
                               BgpPath::PathSource src) {
        return FindSecondaryPath(src_rt, NULL, path_id, src);
    }

    bool RemoveSecondaryPath(const BgpRoute *src_rt,
            const IPeer *peer, uint32_t path_id, BgpPath::PathSource src);
    bool RemoveSecondaryPath(const BgpRoute *src_rt, const IPeer *peer, 
                             BgpPath::PathSource src) {
        return RemoveSecondaryPath(src_rt, peer, 0, src);
    }
    bool RemoveSecondaryPath(const BgpRoute *src_rt, uint32_t path_id, 
                             BgpPath::PathSource src) {
        return RemoveSecondaryPath(src_rt, NULL, path_id, src);
    }

    // Get AFI and SAFI.
    virtual u_int16_t Afi() const = 0;
    virtual u_int8_t Safi() const = 0;

    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  uint32_t label) const = 0;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                      IpAddress nexthop) const = 0;

    // number of paths
    size_t count() const;

    // Fill info needed for introspect
    void FillRouteInfo(BgpTable *table, ShowRoute *show_route);
private:

    DISALLOW_COPY_AND_ASSIGN(BgpRoute);
};

#endif
