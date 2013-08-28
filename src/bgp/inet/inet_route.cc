/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"

using namespace std;
using boost::system::error_code;

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

Ip4Prefix Ip4Prefix::FromString(const std::string &str, error_code *errorp) {
    Ip4Prefix prefix;
    error_code pfxerr = Ip4PrefixParse(str, &prefix.ip4_addr_,
                                       &prefix.prefixlen_);
    if (errorp != NULL) {
        *errorp = pfxerr;
    }
    return prefix;
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

DBEntryBase::KeyPtr InetRoute::GetDBRequestKey() const {
    InetTable::RequestKey *key = new InetTable::RequestKey(prefix_, NULL);
    return KeyPtr(key);
}

void InetRoute::SetKey(const DBRequestKey *reqkey) {
    const InetTable::RequestKey *key =
        static_cast<const InetTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void InetRoute::BuildProtoPrefix(BgpProtoPrefix *prefix, uint32_t label) const {
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
