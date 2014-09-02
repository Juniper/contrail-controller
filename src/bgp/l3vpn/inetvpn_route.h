/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetvpn_route_h
#define ctrlplane_inetvpn_route_h

#include <set>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "bgp/l3vpn/inetvpn_address.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "route/route.h"

class InetVpnRoute : public BgpRoute {
public:
    explicit InetVpnRoute(const InetVpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;

    virtual std::string ToString() const;

    const InetVpnPrefix &GetPrefix() const {
        return prefix_;
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  const BgpAttr *attr = NULL,
                                  uint32_t label = 0) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh, 
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const InetVpnRoute &rhs = static_cast<const InetVpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::IPv4; }
    virtual u_int8_t Safi() const { return BgpAf::Vpn; }
    virtual bool IsMoreSpecific(const std::string &match) const;

private:
    InetVpnPrefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(InetVpnRoute);
};

#endif
