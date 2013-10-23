/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/evpn/evpn_route.h"
#include "bgp/evpn/evpn_table.h"

using namespace std;
using boost::system::error_code;

// E-VPN MAC Advertisement Route Format
//
// +---------------------------------------+
// |      RD   (8 octets)                  |
// +---------------------------------------+
// |Ethernet Segment Identifier (10 octets)|
// +---------------------------------------+
// |  Ethernet Tag ID (4 octets)           |
// +---------------------------------------+
// |  MAC Address Length (1 octet)         |
// +---------------------------------------+
// |  MAC Address (6 octets)               |
// +---------------------------------------+
// |  IP Address Length (1 octet)          |
// +---------------------------------------+
// |  IP Address (4 or 16 octets)          |
// +---------------------------------------+
// |  MPLS Label (3 octets)                |
// +---------------------------------------+

EvpnPrefix::EvpnPrefix(const BgpProtoPrefix &prefix) {
    size_t rd_size = RouteDistinguisher::kSize;
    size_t esi_size = 10;
    size_t tag_size = 4;
    size_t mac_size = MacAddress::kSize;
    size_t ip_size = 4;
    size_t label_size = 3;

    int num_bytes = rd_size + esi_size + tag_size ;
    num_bytes += 1 + mac_size + 1 + ip_size + label_size;
    assert(prefix.prefixlen == (num_bytes * 8));

    size_t rd_offset = 0;
    size_t tag_offset = rd_offset + rd_size + esi_size;
    size_t mac_offset = tag_offset + tag_size + 1;
    size_t ip_offset = mac_offset + mac_size + 1;

    rd_ = RouteDistinguisher(&prefix.prefix[rd_offset]);
    tag_ = get_value(&prefix.prefix[tag_offset], 4);
    mac_addr_ = MacAddress(&prefix.prefix[mac_offset]);

    uint8_t plen = prefix.prefix[ip_offset - 1];
    uint32_t addr = get_value(&prefix.prefix[ip_offset], 4);
    Ip4Address ip4_addr(addr);
    ip_prefix_ = Ip4Prefix(ip4_addr, plen);
}

void EvpnPrefix::BuildProtoPrefix(uint32_t label,
        BgpProtoPrefix *prefix) const {
    size_t rd_size = RouteDistinguisher::kSize;
    size_t esi_size = 10;
    size_t tag_size = 4;
    size_t mac_size = MacAddress::kSize;
    size_t ip_size = 4;
    size_t label_size = 3;

    prefix->prefixlen = (rd_size + esi_size + tag_size) * 8;
    prefix->prefixlen += (1 + mac_size + 1 + ip_size + label_size) * 8;
    int num_bytes = (prefix->prefixlen + 7) / 8;

    prefix->prefix.clear();
    prefix->type = 2;
    prefix->prefix.resize(num_bytes, 0);

    size_t rd_offset = 0;
    std::copy(rd_.GetData(), rd_.GetData() + rd_size,
              prefix->prefix.begin() + rd_offset);

    size_t mac_offset = rd_offset + rd_size + esi_size + tag_size + 1;
    prefix->prefix[mac_offset - 1] = 48;
    std::copy(mac_addr_.GetData(), mac_addr_.GetData() + mac_size,
              prefix->prefix.begin() + mac_offset);

    size_t ip_offset = mac_offset + mac_size + 1;
    prefix->prefix[ip_offset - 1] = ip_prefix_.prefixlen();
    const Ip4Address::bytes_type &addr_bytes = ip_prefix_.ip4_addr().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.begin() + ip_size,
              prefix->prefix.begin() + ip_offset);

    size_t label_offset = ip_offset + ip_size;
    uint32_t tmp = (label << 4 | 0x1);
    for (size_t i = 0; i < label_size; i++) {
        int offset = (label_size - (i + 1)) * 8;
        prefix->prefix[label_offset + i] = ((tmp >> offset) & 0xff);
    }
}

EvpnPrefix EvpnPrefix::FromString(const string &str, error_code *errorp) {
    EvpnPrefix prefix;

    size_t pos1 = str.find('-');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    string rd_str = str.substr(0, pos1);
    error_code rd_err;
    prefix.rd_ = RouteDistinguisher::FromString(rd_str, &rd_err);
    if (rd_err != 0) {
        if (errorp != NULL) {
            *errorp = rd_err;
        }
        return prefix;
    }

    size_t pos2 = str.rfind(',');
    if (pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    string mac_str = str.substr(pos1 + 1, pos2 - pos1 -1);
    error_code mac_err;
    prefix.mac_addr_ = MacAddress::FromString(mac_str, &mac_err);
    if (mac_err != 0) {
        if (errorp != NULL) {
            *errorp = mac_err;
        }
        return prefix;
    }

    string ip_str = str.substr(pos2 + 1, string::npos);
    error_code ip_err;
    prefix.ip_prefix_ = Ip4Prefix::FromString(ip_str, &ip_err);
    if (ip_err != 0) {
        if (errorp != NULL) {
            *errorp = ip_err;
        }
        return prefix;
    }

    return prefix;
}

string EvpnPrefix::ToString() const {
    string str = rd_.ToString();
    str += "-" + mac_addr_.ToString();
    str += "," + ip_prefix_.ToString();
    return str;
}

EvpnRoute::EvpnRoute(const EvpnPrefix &prefix)
    : prefix_(prefix) {
}

int EvpnRoute::CompareTo(const Route &rhs) const {
    const EvpnRoute &other = static_cast<const EvpnRoute &>(rhs);
    KEY_COMPARE(prefix_.route_distinguisher(),
                other.prefix_.route_distinguisher());
    KEY_COMPARE(prefix_.tag(), other.prefix_.tag());
    KEY_COMPARE(prefix_.mac_addr(), other.prefix_.mac_addr());
    KEY_COMPARE(prefix_.ip_prefix(), other.prefix_.ip_prefix());

    return 0;
}

string EvpnRoute::ToString() const {
    return prefix_.ToString();
}

void EvpnRoute::SetKey(const DBRequestKey *reqkey) {
    const EvpnTable::RequestKey *key =
        static_cast<const EvpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void EvpnRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
        uint32_t label) const {
    prefix_.BuildProtoPrefix(label, prefix);
}

void EvpnRoute::BuildBgpProtoNextHop(vector<uint8_t> &nh,
        IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}

DBEntryBase::KeyPtr EvpnRoute::GetDBRequestKey() const {
    EvpnTable::RequestKey *key =
        new EvpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
