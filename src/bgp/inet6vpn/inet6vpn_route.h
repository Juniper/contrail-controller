/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inet6vpn_route_h
#define ctrlplane_inet6vpn_route_h

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "net/rd.h"
#include "route/route.h"

class Inet6VpnPrefix {
public:
    Inet6VpnPrefix();
    Inet6VpnPrefix(const RouteDistinguisher &rd, Ip6Address ip, int prefixlen)
        : rd_(rd), addr_(ip), prefixlen_(prefixlen) {
    }

    static int FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                               Inet6VpnPrefix *prefix, uint32_t *label);
    static Inet6VpnPrefix FromString(const std::string &str,
                                     boost::system::error_code *errorp = NULL);

    std::string ToString() const;
    bool IsMoreSpecific(const Inet6VpnPrefix &rhs) const;
    int CompareTo(const Inet6VpnPrefix &other) const;
    bool operator==(const Inet6VpnPrefix &rhs) const;

    RouteDistinguisher route_distinguisher() const { return rd_; }
    Ip6Address addr() const { return addr_; }
    int prefixlen() const { return prefixlen_; }
    void BuildProtoPrefix(uint32_t label, BgpProtoPrefix *prefix) const;

private:
    RouteDistinguisher rd_;
    Ip6Address addr_;
    int prefixlen_;
};

class Inet6VpnRoute : public BgpRoute {
public:
    explicit Inet6VpnRoute(const Inet6VpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;

    virtual std::string ToString() const;

    const Inet6VpnPrefix &GetPrefix() const {
        return prefix_;
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix, const BgpAttr*,
                                  uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const Inet6VpnRoute &rhs = static_cast<const Inet6VpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::IPv6; }
    virtual u_int8_t Safi() const { return BgpAf::Vpn; }
    virtual bool IsMoreSpecific(const std::string &other) const;

private:
    Inet6VpnPrefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(Inet6VpnRoute);
};

#endif /* ctrlplane_inet6vpn_route_h */
