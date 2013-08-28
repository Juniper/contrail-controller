/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_enet_route_h
#define ctrlplane_enet_route_h

#include <boost/system/error_code.hpp>

#include "bgp/bgp_af.h"
#include "bgp/bgp_route.h"
#include "bgp/inet/inet_route.h"
#include "net/mac_address.h"
#include "net/rd.h"

class EnetPrefix {
public:
    EnetPrefix() { }
    explicit EnetPrefix(const BgpProtoPrefix &prefix);
    EnetPrefix(const MacAddress mac_addr, const Ip4Prefix ip_prefix)
        : mac_addr_(mac_addr), ip_prefix_(ip_prefix) {
    }

    static EnetPrefix FromString(const std::string &str,
            boost::system::error_code *errorp = NULL);
    std::string ToString() const;

    MacAddress mac_addr() const { return mac_addr_; }
    Ip4Prefix ip_prefix() const { return ip_prefix_; }

private:
    MacAddress mac_addr_;
    Ip4Prefix ip_prefix_;
};

class EnetRoute : public BgpRoute {
public:
    EnetRoute(const EnetPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;

    const EnetPrefix &GetPrefix() const { return prefix_; }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);

    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix, uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
            IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const EnetRoute &rhs = static_cast<const EnetRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::L2Vpn; }
    virtual u_int8_t Safi() const { return BgpAf::Enet; }

private:
    EnetPrefix prefix_;

    DISALLOW_COPY_AND_ASSIGN(EnetRoute);
};

#endif
