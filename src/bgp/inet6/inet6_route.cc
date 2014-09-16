/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6/inet6_route.h"
#include "bgp/inet6/inet6_table.h"
#include "base/util.h"

using boost::system::error_code;

Inet6Prefix::Inet6Prefix(const BgpProtoPrefix &prefix)
        : prefixlen_(prefix.prefixlen) {
    assert(prefix.prefixlen <= kMaxV6PrefixLen);
    Ip6Address::bytes_type bt = { { 0 } };
    std::copy(prefix.prefix.begin(), prefix.prefix.end(), bt.begin());
    ip6_addr_ = Ip6Address(bt);
}

std::string Inet6Prefix::ToString() const {
    std::string repr(ip6_addr().to_string());
    repr.append("/" + integerToString(prefixlen()));
    return repr;
}

int Inet6Prefix::CompareTo(const Inet6Prefix &rhs) const {
    if (ip6_addr_ < rhs.ip6_addr_) {
        return -1;
    }
    if (ip6_addr_ > rhs.ip6_addr_) {
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

Inet6Prefix Inet6Prefix::FromString(const std::string &str, error_code *error) {
    Inet6Prefix prefix;
    error_code pfxerr = Inet6PrefixParse(str, &prefix.ip6_addr_,
                                       &prefix.prefixlen_);
    if (error != NULL) {
        *error = pfxerr;
    }
    return prefix;
}

// Check whether 'this' is more specific than rhs.
bool Inet6Prefix::IsMoreSpecific(const Inet6Prefix &rhs) const {

    // My prefixlen must be longer in order to be more specific.
    if (prefixlen_ < rhs.prefixlen()) {
        return false;
    }
    Inet6Prefix mask = Inet6Masks::PrefixlenToMask(rhs.prefixlen());
    Inet6Prefix left = operator&(mask);
    Inet6Prefix right = rhs.operator&(mask);

    return (left.ToBytes() == right.ToBytes());
}

Inet6Prefix Inet6Prefix::operator&(const Inet6Prefix& right) const {
    Ip6Address::bytes_type addr_bytes;
    addr_bytes.assign(0);

    Ip6Address::bytes_type lhs = ToBytes();
    Ip6Address::bytes_type rhs = right.ToBytes();
    for (size_t i = 0; i < sizeof(Ip6Address::bytes_type); ++i) {
        addr_bytes[i] = lhs[i] & rhs[i];
    }
    return Inet6Prefix(Ip6Address(addr_bytes), (prefixlen_ <= right.prefixlen_ ? 
                                              prefixlen() : right.prefixlen()));
}

// Routines for class Inet6Route

Inet6Route::Inet6Route(const Inet6Prefix &prefix) : prefix_(prefix) {
}

int Inet6Route::CompareTo(const Route &rhs) const {
    const Inet6Route &rt_other = static_cast<const Inet6Route &>(rhs);
    return prefix_.CompareTo(rt_other.prefix_);
}

std::string Inet6Route::ToString() const {
    return prefix_.ToString();
}

// Check whether 'this' is more specific than rhs.
bool Inet6Route::IsMoreSpecific(const std::string &match) const {
    error_code ec;

    Inet6Prefix prefix = Inet6Prefix::FromString(match, &ec);
    if (!ec) {
        return GetPrefix().IsMoreSpecific(prefix);
    }

    return false;
}

DBEntryBase::KeyPtr Inet6Route::GetDBRequestKey() const {
    Inet6Table::RequestKey *key = new Inet6Table::RequestKey(prefix_, NULL);
    return KeyPtr(key);
}

void Inet6Route::SetKey(const DBRequestKey *reqkey) {
    const Inet6Table::RequestKey *key =
        static_cast<const Inet6Table::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

// attr and label unused
void Inet6Route::BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  const BgpAttr *attr,
                                  uint32_t label) const {
    prefix->prefixlen = prefix_.prefixlen();
    prefix->prefix.clear();
    const Ip6Address::bytes_type &addr_bytes = prefix_.ip6_addr().to_bytes();
    int num_bytes = (prefix->prefixlen + 7) / 8;
    std::copy(addr_bytes.begin(), addr_bytes.begin() + num_bytes,
         back_inserter(prefix->prefix));
}

void Inet6Route::BuildBgpProtoNextHop(std::vector<uint8_t> &dest_nh,
                                      IpAddress src_nh) const {
    dest_nh.resize(sizeof(Ip6Address::bytes_type));
    const Ip6Address::bytes_type &addr_bytes = src_nh.to_v6().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), dest_nh.begin());
}

// Routines for class Inet6Masks

// Definitions of the static members
bool Inet6Masks::initialized_ = false;
std::vector<Inet6Prefix> Inet6Masks::masks_;

const Inet6Prefix& Inet6Masks::PrefixlenToMask(uint8_t prefix_len) {
    assert(prefix_len <= Inet6Prefix::kMaxV6PrefixLen);
    return masks_.at(prefix_len);
}

void Inet6Masks::Init() {
    assert(initialized_ == false);
    for (int i = 0; i <= Inet6Prefix::kMaxV6PrefixLen; ++i) {
        masks_.push_back(CalculateMaskFromPrefixlen(i));
    }
    initialized_ = true;
}

void Inet6Masks::Clear() {
    masks_.clear();
    initialized_ = false;
}

Inet6Prefix Inet6Masks::CalculateMaskFromPrefixlen(int prefixlen) {
    int num_bytes = prefixlen / 8;
    int num_bits = prefixlen % 8;

    Ip6Address::bytes_type addr_bytes;
    addr_bytes.assign(0);

    for (int i = 0; i < num_bytes; ++i) {
        addr_bytes[i] = 0xff;
    }
    if (num_bits) {
        uint8_t hex_val = 0xff << (8 - num_bits);
        addr_bytes[num_bytes] = hex_val;
    }
    return Inet6Prefix(Ip6Address(addr_bytes), prefixlen);
}

