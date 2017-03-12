/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6vpn/inet6vpn_route.h"

#include <algorithm>

#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/inet6/inet6_route.h"

using std::copy;
using std::string;
using std::vector;

Inet6VpnPrefix::Inet6VpnPrefix() : prefixlen_(0) {
}

int Inet6VpnPrefix::FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                                    Inet6VpnPrefix *prefix, uint32_t *label) {
    size_t nlri_size = proto_prefix.prefix.size();
    size_t expected_min_nlri_size =
        BgpProtoPrefix::kLabelSize + RouteDistinguisher::kSize;

    if (nlri_size < expected_min_nlri_size)
        return -1;
    if (nlri_size > expected_min_nlri_size + Address::kMaxV6Bytes)
        return -1;

    size_t label_offset = 0;
    *label = proto_prefix.ReadLabel(label_offset);
    size_t rd_offset = label_offset + BgpProtoPrefix::kLabelSize;
    prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);

    size_t prefix_offset = rd_offset + RouteDistinguisher::kSize;
    prefix->prefixlen_ = proto_prefix.prefixlen - prefix_offset * 8;
    Ip6Address::bytes_type bt = { { 0 } };
    copy(proto_prefix.prefix.begin() + prefix_offset,
        proto_prefix.prefix.end(), bt.begin());
    prefix->addr_ = Ip6Address(bt);

    return 0;
}

int Inet6VpnPrefix::FromProtoPrefix(BgpServer *server,
                                    const BgpProtoPrefix &proto_prefix,
                                    const BgpAttr *attr, Inet6VpnPrefix *prefix,
                                    BgpAttrPtr *new_attr, uint32_t *label,
                                    uint32_t *l3_label) {
    return FromProtoPrefix(proto_prefix, prefix, label);
}

void Inet6VpnPrefix::BuildProtoPrefix(uint32_t label,
                                      BgpProtoPrefix *proto_prefix) const {
    proto_prefix->prefix.clear();
    size_t prefix_size = (prefixlen_ + 7) / 8;
    size_t nlri_size =
        BgpProtoPrefix::kLabelSize + RouteDistinguisher::kSize + prefix_size;

    proto_prefix->prefix.resize(nlri_size, 0);
    size_t label_offset = 0;
    proto_prefix->WriteLabel(label_offset, label);
    size_t rd_offset = label_offset + BgpProtoPrefix::kLabelSize;
    copy(rd_.GetData(), rd_.GetData() + RouteDistinguisher::kSize,
        proto_prefix->prefix.begin() + rd_offset);

    size_t prefix_offset = rd_offset + RouteDistinguisher::kSize;
    proto_prefix->prefixlen = prefix_offset * 8 + prefixlen_;
    const Ip6Address::bytes_type &addr_bytes = addr_.to_bytes();
    copy(addr_bytes.begin(), addr_bytes.begin() + prefix_size,
        proto_prefix->prefix.begin() + prefix_offset);
}

// RD:inet6-prefix
Inet6VpnPrefix Inet6VpnPrefix::FromString(const string &str,
                                          boost::system::error_code *errorp) {
    Inet6VpnPrefix prefix;

    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    pos = str.find(':', (pos + 1));
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    string rdstr = str.substr(0, pos);
    boost::system::error_code rderr;
    prefix.rd_ = RouteDistinguisher::FromString(rdstr, &rderr);
    if (rderr != 0) {
        if (errorp != NULL) {
            *errorp = rderr;
        }
        return prefix;
    }

    string ip6pstr(str, pos + 1);
    boost::system::error_code pfxerr = Inet6SubnetParse(ip6pstr, &prefix.addr_,
                                                        &prefix.prefixlen_);
    if (errorp != NULL) {
        *errorp = pfxerr;
    }
    return prefix;
}

string Inet6VpnPrefix::ToString() const {
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

string Inet6VpnRoute::ToString() const {
    string repr = prefix_.route_distinguisher().ToString() + ":";
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
                                    uint32_t label,
                                    uint32_t l3_label) const {
    prefix_.BuildProtoPrefix(label, prefix);
}

// XXX dest_nh should have been pointer. See if can change
void Inet6VpnRoute::BuildBgpProtoNextHop(vector<uint8_t> &dest_nh,
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
    copy(addr_bytes.begin(), addr_bytes.end(),
              dest_nh.begin() + RouteDistinguisher::kSize);
}

DBEntryBase::KeyPtr Inet6VpnRoute::GetDBRequestKey() const {
    Inet6VpnTable::RequestKey *key =
        new Inet6VpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}

// Check whether 'this' is more specific than rhs.
bool Inet6VpnRoute::IsMoreSpecific(const string &other) const {
    boost::system::error_code ec;

    Inet6VpnPrefix other_prefix = Inet6VpnPrefix::FromString(other, &ec);
    if (!ec) {
        return GetPrefix().IsMoreSpecific(other_prefix);
    }

    return false;
}

// Check whether 'this' is less specific than rhs.
bool Inet6VpnRoute::IsLessSpecific(const string &other) const {
    boost::system::error_code ec;

    Inet6VpnPrefix other_prefix = Inet6VpnPrefix::FromString(other, &ec);
    if (!ec) {
        return other_prefix.IsMoreSpecific(GetPrefix());
    }

    return false;
}
