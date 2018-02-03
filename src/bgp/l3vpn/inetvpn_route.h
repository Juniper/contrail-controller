/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_L3VPN_INETVPN_ROUTE_H_
#define SRC_BGP_L3VPN_INETVPN_ROUTE_H_

#include <set>
#include <string>
#include <vector>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "bgp/l3vpn/inetvpn_address.h"
#include "net/address.h"
#include "net/bgp_af.h"

class InetVpnRoute : public BgpRoute {
public:
    explicit InetVpnRoute(const InetVpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;

    virtual std::string ToString() const;

    const InetVpnPrefix &GetPrefix() const {
        return prefix_;
    }
    virtual RouteDistinguisher GetRouteDistinguisher() const {
        return prefix_.route_distinguisher();
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  const BgpAttr *attr = NULL,
                                  uint32_t label = 0,
                                  uint32_t l3_label = 0) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const InetVpnRoute &rhs = static_cast<const InetVpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual bool IsMoreSpecific(const std::string &match) const;
    virtual bool IsLessSpecific(const std::string &match) const;

private:
    InetVpnPrefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(InetVpnRoute);
};

#endif  // SRC_BGP_L3VPN_INETVPN_ROUTE_H_
