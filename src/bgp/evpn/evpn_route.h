/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_evpn_route_h
#define ctrlplane_evpn_route_h

#include <boost/system/error_code.hpp>

#include "bgp/bgp_af.h"
#include "bgp/bgp_route.h"
#include "bgp/inet/inet_route.h"
#include "net/mac_address.h"
#include "net/rd.h"

class EvpnPrefix {
public:
    EvpnPrefix() : tag_(0) { }
    explicit EvpnPrefix(const BgpProtoPrefix &prefix);
    EvpnPrefix(const RouteDistinguisher &rd,
        const MacAddress mac_addr, const Ip4Prefix ip_prefix)
        : rd_(rd), tag_(0), mac_addr_(mac_addr), ip_prefix_(ip_prefix) {
    }
    EvpnPrefix(const RouteDistinguisher &rd, uint32_t tag,
        const MacAddress mac_addr, const Ip4Prefix ip_prefix)
        : rd_(rd), tag_(tag), mac_addr_(mac_addr), ip_prefix_(ip_prefix) {
    }

    void BuildProtoPrefix(uint32_t label, BgpProtoPrefix *prefix) const;

    static EvpnPrefix FromString(const std::string &str,
            boost::system::error_code *errorp = NULL);
    std::string ToString() const;

    RouteDistinguisher route_distinguisher() const { return rd_; }
    uint32_t tag() const { return tag_; }
    MacAddress mac_addr() const { return mac_addr_; }
    Ip4Prefix ip_prefix() const { return ip_prefix_; }

private:
    RouteDistinguisher rd_;
    uint32_t tag_;
    MacAddress mac_addr_;
    Ip4Prefix ip_prefix_;
};

class EvpnRoute : public BgpRoute {
public:
    EvpnRoute(const EvpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;

    const EvpnPrefix &GetPrefix() const { return prefix_; }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);

    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix, uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
            IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const EvpnRoute &rhs = static_cast<const EvpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::L2Vpn; }
    virtual u_int8_t Safi() const { return BgpAf::EVpn; }

private:
    EvpnPrefix prefix_;

    DISALLOW_COPY_AND_ASSIGN(EvpnRoute);
};

#endif
