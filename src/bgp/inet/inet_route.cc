/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"

using namespace std;

Ip4Prefix::Ip4Prefix(const BgpProtoPrefix &prefix)
: prefixlen_(prefix.prefixlen) {
    assert(prefix.prefixlen <= 32);
    Ip4Address::bytes_type bt = { { 0 } };
    std::copy(prefix.prefix.begin(), prefix.prefix.end(), bt.begin());
    ip4_addr_ = Ip4Address(bt);
}

string Ip4Prefix::ToString() const {
    string repr(ip4_addr().to_string());
    char strplen[4];
    snprintf(strplen, sizeof(strplen), "/%d", prefixlen());
    repr.append(strplen);
    return repr;
}


int Ip4Prefix::CompareTo(const Ip4Prefix &rhs) const {
    if (ip4_addr_ < rhs.ip4_addr_) {
        return -1;
    }
    if (ip4_addr_ > rhs.ip4_addr_) {
        return 1;
    }
    if (prefixlen_ < rhs.prefixlen_) {
        return -1;
    }
    if (prefixlen_ > rhs.prefixlen_) {
        return 1;
    }
    return 0;
}

Ip4Prefix Ip4Prefix::FromString(const std::string &str, boost::system::error_code *errorp) {
    Ip4Prefix prefix;
    boost::system::error_code pfxerr = Ip4PrefixParse(str, &prefix.ip4_addr_,
                                       &prefix.prefixlen_);
    if (errorp != NULL) {
        *errorp = pfxerr;
    }
    return prefix;
}

// Check whether 'this' is more specific than rhs.
bool Ip4Prefix::IsMoreSpecific(const Ip4Prefix &rhs) const {

    // My prefixlen must be longer in order to be more specific.
    if (prefixlen_ < rhs.prefixlen()) return false;

    uint32_t mask = ((uint32_t) ~0) << (32 - rhs.prefixlen());
    return (ip4_addr_.to_ulong() & mask) ==
        (rhs.ip4_addr().to_ulong() & mask);
}

InetRoute::InetRoute(const Ip4Prefix &prefix) : prefix_(prefix) {
}

int InetRoute::CompareTo(const Route &rhs) const {
    const InetRoute &rt_other = static_cast<const InetRoute &>(rhs);
    return prefix_.CompareTo(rt_other.prefix_);
}

string InetRoute::ToString() const {
    return prefix_.ToString();
}

// Check whether 'this' is more specific than rhs.
bool InetRoute::IsMoreSpecific(const string &match) const {
    boost::system::error_code ec;

    Ip4Prefix prefix = Ip4Prefix::FromString(match, &ec);
    if (!ec) {
        return GetPrefix().IsMoreSpecific(prefix);
    }

    return false;
}

DBEntryBase::KeyPtr InetRoute::GetDBRequestKey() const {
    InetTable::RequestKey *key = new InetTable::RequestKey(prefix_, NULL);
    return KeyPtr(key);
}

void InetRoute::SetKey(const DBRequestKey *reqkey) {
    const InetTable::RequestKey *key =
        static_cast<const InetTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void InetRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
                                 const BgpAttr *attr,
                                 uint32_t label) const {
    prefix->prefixlen = prefix_.prefixlen();
    prefix->prefix.clear();
    const Ip4Address::bytes_type &addr_bytes = prefix_.ip4_addr().to_bytes();
    int num_bytes = (prefix->prefixlen + 7) / 8;
    copy(addr_bytes.begin(), addr_bytes.begin() + num_bytes,
         back_inserter(prefix->prefix));
}

void InetRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &nh, IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}
