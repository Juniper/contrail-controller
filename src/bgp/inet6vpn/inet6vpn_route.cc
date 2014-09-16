/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6vpn/inet6vpn_route.h"

#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/inet6/inet6_route.h"

Inet6VpnPrefix::Inet6VpnPrefix() : prefixlen_(0) {
}

Inet6VpnPrefix::Inet6VpnPrefix(const BgpProtoPrefix &prefix)
  : rd_(&prefix.prefix[3]) {
    size_t labelsize = 3; // 3 bytes label
    size_t rdsize = RouteDistinguisher::kSize;
    int addrsize = Address::kMaxV6Bytes;

    assert(prefix.prefixlen <= ((int) (labelsize + rdsize + addrsize) * 8));
    prefixlen_ = prefix.prefixlen - (rdsize * 8) - (labelsize * 8);
    Ip6Address::bytes_type bt = { { 0 } };
    std::copy(prefix.prefix.begin() + labelsize + rdsize, prefix.prefix.end(),
              bt.begin());
    addr_ = Ip6Address(bt);
}

void Inet6VpnPrefix::BuildProtoPrefix(uint32_t label,
                                      BgpProtoPrefix *prefix) const {
    size_t rdsize = RouteDistinguisher::kSize;
    size_t labelsize = 3; // 3 bytes label

    prefix->prefixlen = prefixlen_ + (rdsize + labelsize) * 8;
    int num_bytes = (prefix->prefixlen + 7) / 8;
    prefix->prefix.clear();
    prefix->prefix.resize(num_bytes);

    uint32_t tmp = (label << 4 | 0x1); // Bottom stack
    for (size_t i = 0; i < labelsize; i++) {
        int offset = (labelsize - (i + 1)) * 8;
        prefix->prefix[i] = ((tmp >> offset) & 0xff);
    }

    std::copy(rd_.GetData(), rd_.GetData() + rdsize,
              prefix->prefix.begin() + labelsize);

    // prefixlen_ includes number of bits in RD and label. Lets calculate number
    // of bytes in the IP address part.
    int num_ip_bytes = num_bytes - rdsize - labelsize;
    const Ip6Address::bytes_type &addr_bytes = addr_.to_bytes();

    std::copy(addr_bytes.begin(), addr_bytes.begin() + num_ip_bytes,
              prefix->prefix.begin() + rdsize+labelsize);
}

// RD:inet6-prefix
Inet6VpnPrefix Inet6VpnPrefix::FromString(const std::string &str,
                                          boost::system::error_code *errorp) {
    Inet6VpnPrefix prefix;

    size_t pos = str.find(':');
    if (pos == std::string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    pos = str.find(':', (pos + 1));
    if (pos == std::string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    std::string rdstr = str.substr(0, pos);
    boost::system::error_code rderr;
    prefix.rd_ = RouteDistinguisher::FromString(rdstr, &rderr);
    if (rderr != 0) {
        if (errorp != NULL) {
            *errorp = rderr;
        }
        return prefix;
    }

    std::string ip6pstr(str, pos + 1);
    boost::system::error_code pfxerr = Inet6PrefixParse(ip6pstr, &prefix.addr_,
                                                        &prefix.prefixlen_);
    if (errorp != NULL) {
        *errorp = pfxerr;
    }
    return prefix;
}

std::string Inet6VpnPrefix::ToString() const {
    Inet6Prefix prefix(addr_, prefixlen_);
    return (rd_.ToString() + ":" + prefix.ToString());
}

// Check whether 'this' is more specific than rhs.
bool Inet6VpnPrefix::IsMoreSpecific(const Inet6VpnPrefix &rhs) const {
    Inet6Prefix this_prefix(addr_, prefixlen_);
    Inet6Prefix match_prefix(rhs.addr(), rhs.prefixlen());

    return this_prefix.IsMoreSpecific(match_prefix);
}

int Inet6VpnPrefix::CompareTo(const Inet6VpnPrefix &other) const {

    int res = route_distinguisher().CompareTo(other.route_distinguisher());
    if (res != 0) {
        return res;
    }
    Ip6Address laddr = addr();
    Ip6Address raddr = other.addr();
    if (laddr < raddr) {
        return -1;
    }
    if (laddr > raddr) {
        return 1;
    }
    if (prefixlen() < other.prefixlen()) {
        return -1;
    }
    if (prefixlen() > other.prefixlen()) {
        return 1;
    }
    return 0;
}

bool Inet6VpnPrefix::operator==(const Inet6VpnPrefix &rhs) const {
    return (rd_ == rhs.rd_ && addr_ == rhs.addr_ &&
            prefixlen_ == rhs.prefixlen_);
}

Inet6VpnRoute::Inet6VpnRoute(const Inet6VpnPrefix &prefix) : prefix_(prefix) {
}

int Inet6VpnRoute::CompareTo(const Route &rhs) const {
    const Inet6VpnRoute &other = static_cast<const Inet6VpnRoute &>(rhs);
    return prefix_.CompareTo(other.GetPrefix());
}

std::string Inet6VpnRoute::ToString() const {
    std::string repr = prefix_.route_distinguisher().ToString() + ":";
    repr += prefix_.addr().to_string();
    char strplen[5];
    snprintf(strplen, sizeof(strplen), "/%d", prefix_.prefixlen());
    repr.append(strplen);

    return repr;
}

void Inet6VpnRoute::SetKey(const DBRequestKey *reqkey) {
    const Inet6VpnTable::RequestKey *key =
        static_cast<const Inet6VpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void Inet6VpnRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
                                     const BgpAttr*,
                                    uint32_t label) const {
    prefix_.BuildProtoPrefix(label, prefix);
}

// XXX dest_nh should have been pointer. See if can change
void Inet6VpnRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &dest_nh,
                                         IpAddress src_nh) const {
    dest_nh.resize(sizeof(Ip6Address::bytes_type) + RouteDistinguisher::kSize,
                   0);
    Ip6Address source_addr;
    if (src_nh.is_v4()) {
        source_addr = Ip6Address::v4_mapped(src_nh.to_v4());
    } else if (src_nh.is_v6()) {
        source_addr = src_nh.to_v6();
    } else {
        assert(0);
    }

    Ip6Address::bytes_type addr_bytes = source_addr.to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(),
              dest_nh.begin() + RouteDistinguisher::kSize);
}

DBEntryBase::KeyPtr Inet6VpnRoute::GetDBRequestKey() const {
    Inet6VpnTable::RequestKey *key =
        new Inet6VpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}

// Check whether 'this' is more specific than rhs.
bool Inet6VpnRoute::IsMoreSpecific(const std::string &other) const {
    boost::system::error_code ec;

    Inet6VpnPrefix other_prefix = Inet6VpnPrefix::FromString(other, &ec);
    if (!ec) {
        return GetPrefix().IsMoreSpecific(other_prefix);
    }

    return false;
}
