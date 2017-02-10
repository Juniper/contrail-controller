/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet/inet_route.h"

#include <algorithm>

#include "bgp/inet/inet_table.h"

using std::copy;
using std::string;
using std::vector;

int Ip4Prefix::FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                               Ip4Prefix *prefix) {
    if (proto_prefix.prefix.size() > Address::kMaxV4Bytes)
        return -1;
    prefix->prefixlen_ = proto_prefix.prefixlen;
    Ip4Address::bytes_type bt = { { 0 } };
    copy(proto_prefix.prefix.begin(), proto_prefix.prefix.end(), bt.begin());
    prefix->ip4_addr_ = Ip4Address(bt);

    return 0;
}

int Ip4Prefix::FromProtoPrefix(BgpServer *server,
    const BgpProtoPrefix &proto_prefix, const BgpAttr *attr, Ip4Prefix *prefix,
    BgpAttrPtr *new_attr, uint32_t *label, uint32_t *l3_label) {
    return FromProtoPrefix(proto_prefix, prefix);
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

Ip4Prefix Ip4Prefix::FromString(const string &str,
                                boost::system::error_code *errorp) {
    Ip4Prefix prefix;
    boost::system::error_code pfxerr = Ip4SubnetParse(str, &prefix.ip4_addr_,
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

    uint32_t mask = 0;
    if (rhs.prefixlen())
       mask = ((uint32_t) ~0) << (Address::kMaxV4PrefixLen - rhs.prefixlen());
    return (ip4_addr_.to_ulong() & mask) ==
        (rhs.ip4_addr().to_ulong() & mask);
}

InetRoute::InetRoute(const Ip4Prefix &prefix)
    : prefix_(prefix),
      prefix_str_(prefix.ToString()) {
}

int InetRoute::CompareTo(const Route &rhs) const {
    const InetRoute &rt_other = static_cast<const InetRoute &>(rhs);
    return prefix_.CompareTo(rt_other.prefix_);
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

// Check whether 'this' is less specific than rhs.
bool InetRoute::IsLessSpecific(const string &match) const {
    boost::system::error_code ec;

    Ip4Prefix prefix = Ip4Prefix::FromString(match, &ec);
    if (!ec) {
        return prefix.IsMoreSpecific(GetPrefix());
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
                                 uint32_t label,
                                 uint32_t l3_label) const {
    prefix->prefixlen = prefix_.prefixlen();
    prefix->prefix.clear();
    const Ip4Address::bytes_type &addr_bytes = prefix_.ip4_addr().to_bytes();
    int num_bytes = (prefix->prefixlen + 7) / 8;
    copy(addr_bytes.begin(), addr_bytes.begin() + num_bytes,
         back_inserter(prefix->prefix));
}

void InetRoute::BuildBgpProtoNextHop(vector<uint8_t> &nh,
                                     IpAddress nexthop) const {
    nh.resize(Address::kMaxV4Bytes);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}
