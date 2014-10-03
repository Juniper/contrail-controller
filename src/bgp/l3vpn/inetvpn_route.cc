/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/inet/inet_route.h"

using namespace std;

InetVpnRoute::InetVpnRoute(const InetVpnPrefix &prefix)
    : prefix_(prefix) {
}

int InetVpnRoute::CompareTo(const Route &rhs) const {
    const InetVpnRoute &other = static_cast<const InetVpnRoute &>(rhs);
    int res = prefix_.route_distinguisher().CompareTo(
        other.prefix_.route_distinguisher());
    if (res != 0) {
        return res;
    }
    Ip4Address laddr = prefix_.addr();
    Ip4Address raddr = other.prefix_.addr();
    if (laddr < raddr) {
        return -1;
    }
    if (laddr > raddr) {
        return 1;
    }
    if (prefix_.prefixlen() < other.prefix_.prefixlen()) {
        return -1;
    }
    if (prefix_.prefixlen() > other.prefix_.prefixlen()) {
        return 1;
    }
    return 0;
}

string InetVpnRoute::ToString() const {
    string repr = prefix_.route_distinguisher().ToString() + ":";
    repr += prefix_.addr().to_string();
    char strplen[4];
    snprintf(strplen, sizeof(strplen), "/%d", prefix_.prefixlen());
    repr.append(strplen);

    return repr;
}

void InetVpnRoute::SetKey(const DBRequestKey *reqkey) {
    const InetVpnTable::RequestKey *key =
        static_cast<const InetVpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void InetVpnRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
                                    const BgpAttr *attr,
                                    uint32_t label) const {
    prefix_.BuildProtoPrefix(label, prefix);
}

void InetVpnRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &nh, IpAddress nexthop) const {
    nh.resize(4+RouteDistinguisher::kSize);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin()+RouteDistinguisher::kSize);
}

DBEntryBase::KeyPtr InetVpnRoute::GetDBRequestKey() const {
    InetVpnTable::RequestKey *key = new InetVpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}

// Check whether 'this' is more specific than rhs.
bool InetVpnRoute::IsMoreSpecific(const string &match) const {
    boost::system::error_code ec;

    InetVpnPrefix prefix = InetVpnPrefix::FromString(match, &ec);
    if (!ec) {
        return GetPrefix().IsMoreSpecific(prefix);
    }

    return false;
}
