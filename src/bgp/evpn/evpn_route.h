/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EVPN_EVPN_ROUTE_H_
#define SRC_BGP_EVPN_EVPN_ROUTE_H_

#include <boost/system/error_code.hpp>

#include <string>
#include <vector>

#include "bgp/bgp_route.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "net/esi.h"
#include "net/mac_address.h"
#include "net/rd.h"

class BgpServer;

class EvpnPrefix {
public:
    static const EvpnPrefix kNullPrefix;
    static const uint32_t kInvalidLabel;
    static const uint32_t kNullTag;
    static const uint32_t kMaxTag;
    static const uint32_t kMaxVni;

    static const size_t kRdSize;
    static const size_t kEsiSize;
    static const size_t kTagSize;
    static const size_t kIp4AddrSize;
    static const size_t kIp6AddrSize;
    static const size_t kMacSize;
    static const size_t kLabelSize;
    static const size_t kMinAutoDiscoveryRouteSize;
    static const size_t kMinMacAdvertisementRouteSize;
    static const size_t kMinInclusiveMulticastRouteSize;
    static const size_t kMinSegmentRouteSize;
    static const size_t kMinInetPrefixRouteSize;
    static const size_t kMinInet6PrefixRouteSize;

    enum RouteType {
        Unspecified = 0,
        AutoDiscoveryRoute = 1,
        MacAdvertisementRoute = 2,
        InclusiveMulticastRoute = 3,
        SegmentRoute = 4,
        IpPrefixRoute = 5
    };

    EvpnPrefix();
    EvpnPrefix(const RouteDistinguisher &rd, const EthernetSegmentId &esi,
        uint32_t tag);
    EvpnPrefix(const RouteDistinguisher &rd,
        const MacAddress &mac_addr, const IpAddress &ip_address);
    EvpnPrefix(const RouteDistinguisher &rd, uint32_t tag,
        const MacAddress &mac_addr, const IpAddress &ip_address);
    EvpnPrefix(const RouteDistinguisher &rd, uint32_t tag,
        const IpAddress &ip_address);
    EvpnPrefix(const RouteDistinguisher &rd, const EthernetSegmentId &esi,
        const IpAddress &ip_address);
    EvpnPrefix(const RouteDistinguisher &rd, uint32_t tag,
        const IpAddress &ip_address, uint8_t ip_prefixlen);

    void BuildProtoPrefix(BgpProtoPrefix *proto_prefix,
        const BgpAttr *attr, uint32_t label, uint32_t l3_label = 0) const;

    static int FromProtoPrefix(BgpServer *server,
        const BgpProtoPrefix &proto_prefix, const BgpAttr *attr,
        EvpnPrefix *evpn_prefix, BgpAttrPtr *new_attr, uint32_t *label,
        uint32_t *l3_label = NULL);
    static EvpnPrefix FromString(const std::string &str,
        boost::system::error_code *errorp = NULL);
    std::string ToString() const;
    std::string ToXmppIdString() const;

    int CompareTo(const EvpnPrefix &rhs) const;
    bool operator==(const EvpnPrefix &rhs) const { return CompareTo(rhs) == 0; }

    uint8_t type() const { return type_; }
    const RouteDistinguisher &route_distinguisher() const { return rd_; }
    const EthernetSegmentId &esi() const { return esi_; }
    uint32_t tag() const { return tag_; }
    const MacAddress &mac_addr() const { return mac_addr_; }
    Address::Family family() const { return family_; }
    IpAddress ip_address() const { return ip_address_; }
    uint8_t ip_address_length() const;
    uint8_t ip_prefix_length() const { return ip_prefixlen_; }
    Ip4Prefix inet_prefix() const {
        return Ip4Prefix(ip_address_.to_v4(), ip_prefixlen_);
    }
    Inet6Prefix inet6_prefix() const {
        return Inet6Prefix(ip_address_.to_v6(), ip_prefixlen_);
    }
    void set_route_distinguisher(const RouteDistinguisher &rd) { rd_ = rd; }

private:
    uint8_t type_;
    RouteDistinguisher rd_;
    EthernetSegmentId esi_;
    uint32_t tag_;
    MacAddress mac_addr_;
    Address::Family family_;
    IpAddress ip_address_;
    uint8_t ip_prefixlen_;

    size_t GetIpAddressSize() const;
    void ReadIpAddress(const BgpProtoPrefix &proto_prefix,
        size_t ip_offset, size_t ip_size, size_t ip_psize);
    void WriteIpAddress(BgpProtoPrefix *proto_prefix, size_t ip_offset) const;
};

class EvpnRoute : public BgpRoute {
public:
    explicit EvpnRoute(const EvpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual std::string ToXmppIdString() const;
    virtual bool IsValid() const;

    const EvpnPrefix &GetPrefix() const { return prefix_; }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);

    virtual void BuildProtoPrefix(BgpProtoPrefix *proto_prefix,
        const BgpAttr *attr = NULL, uint32_t label = 0,
        uint32_t l3_label = 0) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
        IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const EvpnRoute &rhs = static_cast<const EvpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::L2Vpn; }
    virtual u_int8_t Safi() const { return BgpAf::EVpn; }
    virtual u_int8_t XmppSafi() const { return BgpAf::Enet; }

private:
    EvpnPrefix prefix_;
    mutable std::string xmpp_id_str_;

    DISALLOW_COPY_AND_ASSIGN(EvpnRoute);
};

#endif  // SRC_BGP_EVPN_EVPN_ROUTE_H_
