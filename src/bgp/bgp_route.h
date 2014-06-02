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

    BgpPath *FindPath(BgpPath::PathSource src, const IPeer *peer,
                      uint32_t path_id);
    bool RemovePath(BgpPath::PathSource src, const IPeer *peer = NULL,
                    uint32_t path_id = 0);
    bool RemovePath(const IPeer *peer);

    virtual bool IsValid() const;

    // Check if there's a better path with the same forwarding information.
    bool DuplicateForwardingPath(const BgpPath *in_path) const;

    BgpPath *FindSecondaryPath(BgpRoute *src_rt, BgpPath::PathSource src,
            const IPeer *peer, uint32_t path_id);
    bool RemoveSecondaryPath(const BgpRoute *src_rt, BgpPath::PathSource src,
            const IPeer *peer, uint32_t path_id);

    // Get AFI and SAFI.
    virtual u_int16_t Afi() const = 0;
    virtual u_int8_t Safi() const = 0;
    virtual u_int8_t XmppSafi() const { return Safi(); }
    virtual std::string ToXmppIdString() const { return ToString(); }

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
