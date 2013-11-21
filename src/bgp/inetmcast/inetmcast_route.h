/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetmcast_route_h
#define ctrlplane_inetmcast_route_h

#include "bgp/bgp_attr.h"
#include "bgp/bgp_route.h"
#include "net/bgp_af.h"
#include "net/rd.h"

class InetMcastPrefix {
public:
    InetMcastPrefix();
    InetMcastPrefix(const RouteDistinguisher &rd,
            const Ip4Address &group, const Ip4Address &source)
        : rd_(rd), group_(group), source_(source) {
    }
    static InetMcastPrefix FromString(const std::string &str,
                                      boost::system::error_code *errorp = NULL);
    std::string ToString() const;
    std::string ToXmppIdString() const;

    RouteDistinguisher route_distinguisher() const { return rd_; }
    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }

private:
    RouteDistinguisher rd_;
    Ip4Address group_, source_;
};

class InetMcastRoute : public BgpRoute {
public:
    InetMcastRoute(const InetMcastPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;

    const InetMcastPrefix &GetPrefix() const {
        return prefix_;
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);

    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix, uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh, 
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const InetMcastRoute &rhs = static_cast<const InetMcastRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::IPv4; }
    virtual u_int8_t Safi() const { return BgpAf::Mcast; }

private:
    InetMcastPrefix prefix_;

    DISALLOW_COPY_AND_ASSIGN(InetMcastRoute);
};

#endif
