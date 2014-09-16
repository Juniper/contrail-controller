/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/l3vpn/inetvpn_address.h"

#include "bgp/inet/inet_route.h"

using namespace std;

InetVpnPrefix::InetVpnPrefix()
  : prefixlen_(0) {
}

InetVpnPrefix::InetVpnPrefix(const BgpProtoPrefix &prefix)
  : rd_(&prefix.prefix[3]) {
    size_t rdsize = RouteDistinguisher::kSize;
    size_t labelsize = 3; // 3 bytes label

    assert(prefix.prefixlen <= (int) (rdsize + 4 + labelsize) * 8);
    prefixlen_ = prefix.prefixlen - rdsize * 8 - labelsize * 8;
    Ip4Address::bytes_type bt = { { 0 } };
    std::copy(prefix.prefix.begin() + labelsize + rdsize, prefix.prefix.end(), bt.begin());
    addr_ = Ip4Address(bt);
}

void InetVpnPrefix::BuildProtoPrefix(uint32_t label, BgpProtoPrefix *prefix) const {
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
              prefix->prefix.begin()+labelsize);

    // prefixlen_ includes number of bits in RD and label. Lets calculate number of
    // bytes in the IP address part.
    int num_ip_bytes = num_bytes - rdsize - labelsize;
    const Ip4Address::bytes_type &addr_bytes = addr_.to_bytes();

    std::copy(addr_bytes.begin(), addr_bytes.begin() + num_ip_bytes,
              prefix->prefix.begin()+rdsize+labelsize);
}

// RD:inet4-prefix
InetVpnPrefix InetVpnPrefix::FromString(const string &str, boost::system::error_code *errorp) {
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

