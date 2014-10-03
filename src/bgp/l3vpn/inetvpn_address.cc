/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/l3vpn/inetvpn_address.h"

#include "bgp/inet/inet_route.h"

using std::copy;
using std::string;

InetVpnPrefix::InetVpnPrefix() : prefixlen_(0) {
}

int InetVpnPrefix::FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                                   InetVpnPrefix *prefix, uint32_t *label) {
    size_t nlri_size = proto_prefix.prefix.size();
    size_t expected_min_nlri_size =
        BgpProtoPrefix::kLabelSize + RouteDistinguisher::kSize;

    if (nlri_size < expected_min_nlri_size)
        return -1;
    if (nlri_size > expected_min_nlri_size + Address::kMaxV4Bytes)
        return -1;

    size_t label_offset = 0;
    *label = proto_prefix.ReadLabel(label_offset);
    size_t rd_offset = label_offset + BgpProtoPrefix::kLabelSize;
    prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);

    size_t prefix_offset = rd_offset + RouteDistinguisher::kSize;
    prefix->prefixlen_ = proto_prefix.prefixlen - prefix_offset * 8;
    Ip4Address::bytes_type bt = { { 0 } };
    copy(proto_prefix.prefix.begin() + prefix_offset,
        proto_prefix.prefix.end(), bt.begin());
    prefix->addr_ = Ip4Address(bt);

    return 0;
}

void InetVpnPrefix::BuildProtoPrefix(uint32_t label,
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
    const Ip4Address::bytes_type &addr_bytes = addr_.to_bytes();
    copy(addr_bytes.begin(), addr_bytes.begin() + prefix_size,
        proto_prefix->prefix.begin() + prefix_offset);
}

// RD:inet4-prefix
InetVpnPrefix InetVpnPrefix::FromString(const string &str,
                                        boost::system::error_code *errorp) {
    InetVpnPrefix prefix;

    size_t pos = str.rfind(':');
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

    string ip4pstr(str, pos + 1);
    boost::system::error_code pfxerr = Ip4PrefixParse(ip4pstr, &prefix.addr_,
                                       &prefix.prefixlen_);
    if (errorp != NULL) {
        *errorp = pfxerr;
    }
    return prefix;
}

string InetVpnPrefix::ToString() const {
    Ip4Prefix prefix(addr_, prefixlen_);
    return (rd_.ToString() + ":" + prefix.ToString());
}

// Check whether 'this' is more specific than rhs.
bool InetVpnPrefix::IsMoreSpecific(const InetVpnPrefix &rhs) const {
    Ip4Prefix this_prefix(addr_, prefixlen_);
    Ip4Prefix match_prefix(rhs.addr(), rhs.prefixlen());

    return this_prefix.IsMoreSpecific(match_prefix);
}

bool InetVpnPrefix::operator==(const InetVpnPrefix &rhs) const {
    return (rd_ == rhs.rd_ &&
        addr_ == rhs.addr_ &&
        prefixlen_ == rhs.prefixlen_);
}
