/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ermvpn_route_h
#define ctrlplane_ermvpn_route_h

#include <set>

#include <boost/system/error_code.hpp>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "net/rd.h"

class ErmVpnPrefix {
public:
    enum RouteType {
        NativeRoute = 0,
        LocalTreeRoute = 1,
        GlobalTreeRoute = 2,
        Invalid = 255,
    };

    ErmVpnPrefix();
    explicit ErmVpnPrefix(const BgpProtoPrefix &prefix);
    ErmVpnPrefix(uint8_t type, const RouteDistinguisher &rd,
                 const Ip4Address &group, const Ip4Address &source);
    ErmVpnPrefix(uint8_t type, const RouteDistinguisher &rd,
                 const Ip4Address &router_id,
                 const Ip4Address &group, const Ip4Address &source);
    static ErmVpnPrefix FromString(const std::string &str,
                                   boost::system::error_code *errorp = NULL);
    std::string ToString() const;
    std::string ToXmppIdString() const;
    static bool IsValidForBgp(uint8_t type);
    static bool IsValid(uint8_t type);
    bool operator==(const ErmVpnPrefix &rhs) const;

    uint8_t type() const { return type_; }
    RouteDistinguisher route_distinguisher() const { return rd_; }
    Ip4Address router_id() const { return router_id_; }
    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }
    void set_route_distinguisher(RouteDistinguisher rd) { rd_ = rd; }

    void BuildProtoPrefix(BgpProtoPrefix *prefix) const;

private:
    uint8_t type_;
    RouteDistinguisher rd_;
    Ip4Address router_id_;
    Ip4Address group_;
    Ip4Address source_;
};

class ErmVpnRoute : public BgpRoute {
public:
    explicit ErmVpnRoute(const ErmVpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;

    virtual std::string ToString() const;
    virtual std::string ToXmppIdString() const;
    virtual bool IsValid() const;

    const ErmVpnPrefix &GetPrefix() const { return prefix_; }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh, 
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const ErmVpnRoute &rhs = static_cast<const ErmVpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::IPv4; }
    virtual u_int8_t Safi() const { return BgpAf::ErmVpn; }
    virtual u_int8_t XmppSafi() const { return BgpAf::Mcast; }

private:
    ErmVpnPrefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(ErmVpnRoute);
};

#endif
