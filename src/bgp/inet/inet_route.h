/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inet_route_h
#define ctrlplane_inet_route_h

#include "bgp/bgp_attr.h"
#include "bgp/bgp_route.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "route/route.h"

class Ip4Prefix {
public:
    Ip4Prefix() : prefixlen_ (0) { }
    Ip4Prefix(Ip4Address addr, int prefixlen)
        : ip4_addr_(addr), prefixlen_(prefixlen) {
    }
    explicit Ip4Prefix(const BgpProtoPrefix &prefix);
    int CompareTo(const Ip4Prefix &rhs) const;

    Ip4Address ip4_addr() const { return ip4_addr_; }
    
    int prefixlen() const { return prefixlen_; }

    static Ip4Prefix FromString(const std::string &str,
                                boost::system::error_code *errorp = NULL);
    std::string ToString() const;

    bool operator==(const Ip4Prefix &rhs) const {
        return (CompareTo(rhs) == 0);
    }
    bool operator!=(const Ip4Prefix &rhs) const {
        return (CompareTo(rhs) != 0);
    }
    bool operator<(const Ip4Prefix &rhs) const {
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }
    bool operator>(const Ip4Prefix &rhs) const {
        int cmp = CompareTo(rhs);
        return (cmp > 0);
    }

    // Check whether 'this' is more specific than rhs.
    bool IsMoreSpecific(const Ip4Prefix &rhs) const;

private:
    Ip4Address ip4_addr_;
    int prefixlen_;
};

class InetRoute : public BgpRoute {
public:
    explicit InetRoute(const Ip4Prefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;

    const Ip4Prefix &GetPrefix() const {
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
        const InetRoute &rhs = static_cast<const InetRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    // Check whether 'this' is more specific than rhs.
    virtual bool IsMoreSpecific(const std::string &match) const;
    virtual u_int16_t Afi() const { return BgpAf::IPv4; }
    virtual u_int8_t Safi() const { return BgpAf::Unicast; }

private:
    Ip4Prefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(InetRoute);
};

#endif
