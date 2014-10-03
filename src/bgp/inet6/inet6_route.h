/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inet6_route_h
#define ctrlplane_inet6_route_h

#include "bgp/bgp_attr.h"
#include "bgp/bgp_route.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "route/route.h"

class Inet6Prefix {
public:
    static const uint8_t kMaxV6PrefixLen = Address::kMaxV6PrefixLen;

    Inet6Prefix() : prefixlen_ (0) { }
    Inet6Prefix(Ip6Address addr, int prefixlen)
        : ip6_addr_(addr), prefixlen_(prefixlen) {
    }
    explicit Inet6Prefix(const BgpProtoPrefix &prefix);
    int CompareTo(const Inet6Prefix &rhs) const;

    Ip6Address ip6_addr() const { return ip6_addr_; }

    const Ip6Address::bytes_type ToBytes() const {
        return ip6_addr_.to_bytes();
    }

    int prefixlen() const { return prefixlen_; }

    static Inet6Prefix FromString(const std::string &str,
                                boost::system::error_code *errorp = NULL);
    std::string ToString() const;

    bool operator==(const Inet6Prefix &rhs) const {
        return (CompareTo(rhs) == 0);
    }
    bool operator!=(const Inet6Prefix &rhs) const {
        return (CompareTo(rhs) != 0);
    }
    bool operator<(const Inet6Prefix &rhs) const {
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }
    bool operator>(const Inet6Prefix &rhs) const {
        int cmp = CompareTo(rhs);
        return (cmp > 0);
    }

    // Check whether 'this' is more specific than rhs.
    bool IsMoreSpecific(const Inet6Prefix &rhs) const;

    Inet6Prefix operator&(const Inet6Prefix& rhs) const;

private:
    Ip6Address::bytes_type ToBytes() { return ip6_addr_.to_bytes(); }

    Ip6Address ip6_addr_;
    int prefixlen_;
};

class Inet6Route : public BgpRoute {
public:
    explicit Inet6Route(const Inet6Prefix &prefix);
    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;

    const Inet6Prefix &GetPrefix() const {
        return prefix_;
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);

    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix, const BgpAttr *attr,
                                  uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &dest,
                                      IpAddress src) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const Inet6Route &rhs = static_cast<const Inet6Route &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    // Check whether 'this' is more specific than rhs.
    virtual bool IsMoreSpecific(const std::string &match) const;
    virtual u_int16_t Afi() const { return BgpAf::IPv6; }
    virtual u_int8_t Safi() const { return BgpAf::Unicast; }
    virtual u_int16_t NexthopAfi() const { return BgpAf::IPv4; }

private:
    Inet6Prefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(Inet6Route);
};

class Inet6Masks {
public:
    static void Init();
    static void Clear();
    static const Inet6Prefix& PrefixlenToMask(uint8_t prefix_len);
private:
    static Inet6Prefix CalculateMaskFromPrefixlen(int prefixlen);

    static std::vector<Inet6Prefix> masks_;
    static bool initialized_;
};

#endif /* #ifndef ctrlplane_inet6_route_h */
