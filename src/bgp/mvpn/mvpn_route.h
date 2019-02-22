/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_MVPN_MVPN_ROUTE_H_
#define SRC_BGP_MVPN_MVPN_ROUTE_H_


#include <set>
#include <string>
#include <vector>

#include <boost/system/error_code.hpp>

#include "base/util.h"
#include "base/address.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "net/bgp_af.h"
#include "net/rd.h"

class MvpnPrefix {
public:

    static const size_t kRdSize;
    static const size_t kIp4AddrSize;
    static const size_t kIp4AddrBitSize;
    static const size_t kAsnSize;
    static const size_t kPrefixBytes;
    static const size_t kIntraASPMSIADRouteSize;
    static const size_t kInterASPMSIADRouteSize;
    static const size_t kSPMSIADRouteSize;
    static const size_t kLeafADRouteSize;
    static const size_t kSourceActiveADRouteSize;
    static const size_t kSourceTreeJoinRouteSize;

    enum RouteType {
        Unspecified = 0,
        IntraASPMSIADRoute = 1,
        InterASPMSIADRoute = 2,
        SPMSIADRoute = 3,
        LeafADRoute = 4,
        SourceActiveADRoute = 5,
        SharedTreeJoinRoute = 6,
        SourceTreeJoinRoute = 7,
    };

    MvpnPrefix();
    MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
               const Ip4Address &originator);
    MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
               const uint32_t asn);
    MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
               const Ip4Address &group, const Ip4Address &source);
    MvpnPrefix(uint8_t type, const Ip4Address &originator);
    MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
               const Ip4Address &originator,
               const Ip4Address &group, const Ip4Address &source);
    MvpnPrefix(uint8_t type, const RouteDistinguisher &rd, const uint32_t asn,
               const Ip4Address &group, const Ip4Address &source);

    Ip4Address GetType3OriginatorFromType4Route() const;
    void SetLeafADPrefixFromSPMSIPrefix(const MvpnPrefix &prefix);
    void SetSPMSIPrefixFromLeafADPrefix(const MvpnPrefix &prefix);

    std::string ToString() const;
    std::string ToXmppIdString() const;
    bool operator==(const MvpnPrefix &rhs) const;
    int CompareTo(const MvpnPrefix &rhs) const;

    uint8_t type() const { return type_; }
    const RouteDistinguisher &route_distinguisher() const { return rd_; }
    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }
    Ip4Address originator() const { return originator_; }
    IpAddress groupIpAddress() const { return IpAddress(group_); }
    IpAddress sourceIpAddress() const { return IpAddress(source_); }
    IpAddress originatorIpAddress() const { return IpAddress(originator_); }
    void set_originator(const Ip4Address &originator);
    uint32_t asn() const { return asn_; }
    void set_route_distinguisher(const RouteDistinguisher &rd) { rd_ = rd; }
    uint8_t ip_prefix_length() const { return ip_prefixlen_; }
    void BuildProtoPrefix(BgpProtoPrefix *prefix) const;
    const std::string GetType() const;

    static int FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                               MvpnPrefix *prefix);
    static int FromProtoPrefix(BgpServer *server,
                               const BgpProtoPrefix &proto_prefix,
                               const BgpAttr *attr,
                               const Address::Family family,
                               MvpnPrefix *prefix,
                               BgpAttrPtr *new_attr, uint32_t *label,
                               uint32_t *l3_label);
    static MvpnPrefix FromString(const std::string &str,
                                 boost::system::error_code *errorp = NULL);
    static bool IsValid(uint8_t type);

private:
    static bool GetTypeFromString(MvpnPrefix *prefix,
            const std::string &str, boost::system::error_code *errorp,
            size_t *pos1);
    static bool GetRDFromString(MvpnPrefix *prefix,
            const std::string &str, size_t pos1, size_t *pos2,
            boost::system::error_code *ec);
    static bool GetOriginatorFromString(MvpnPrefix *prefix,
            const std::string &str, size_t pos1,
            boost::system::error_code *errorp);
    static bool GetAsnFromString(MvpnPrefix *prefix,
            const std::string &str, size_t pos1, size_t *pos2,
            boost::system::error_code *ec);
    static bool GetSourceFromString(MvpnPrefix *prefix,
            const std::string &str, size_t pos1, size_t *pos2,
            boost::system::error_code *ec);
    static bool GetGroupFromString(MvpnPrefix *prefix,
            const std::string &str, size_t pos1, size_t *pos2,
            boost::system::error_code *ec, bool last = false);
    static int SpmsiAdRouteFromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                               MvpnPrefix *prefix, size_t rd_offset);
    uint8_t type_;
    RouteDistinguisher rd_;
    Ip4Address originator_;
    Ip4Address group_;
    Ip4Address source_;
    uint8_t ip_prefixlen_;
    uint32_t asn_;
    std::vector<uint8_t> rt_key_;
};

class MvpnRoute : public BgpRoute {
public:
    explicit MvpnRoute(const MvpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual std::string ToXmppIdString() const;
    virtual bool IsValid() const;

    const MvpnPrefix &GetPrefix() const { return prefix_; }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  const BgpAttr *attr = NULL,
                                  uint32_t label = 0,
                                  uint32_t l3_label = 0) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const MvpnRoute &rhs = static_cast<const MvpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    const std::string GetType() const;

private:
    MvpnPrefix prefix_;
    mutable std::string xmpp_id_str_;

    DISALLOW_COPY_AND_ASSIGN(MvpnRoute);
};

#endif  // SRC_BGP_MVPN_MVPN_ROUTE_H_
