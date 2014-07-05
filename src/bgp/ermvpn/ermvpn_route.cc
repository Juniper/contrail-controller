/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/parse_object.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/ermvpn/ermvpn_table.h"

using namespace std;

// BgpProtoPrefix format for erm-vpn prefix.
//
// +------------------------------------+
// |      RD   (8 octets)               |
// +------------------------------------+
// |  Router-Id (4 octets)              |
// +------------------------------------+
// |  Multicast Source length (1 octet) |
// +------------------------------------+
// |  Multicast Source (4)              |
// +------------------------------------+
// |  Multicast Group length (1 octet)  |
// +------------------------------------+
// |  Multicast Group (4)               |
// +------------------------------------+

ErmVpnPrefix::ErmVpnPrefix() : type_(ErmVpnPrefix::Invalid) {
}

ErmVpnPrefix::ErmVpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), group_(group), source_(source) {
}

ErmVpnPrefix::ErmVpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &router_id,
    const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), router_id_(router_id),
      group_(group), source_(source) {
}

ErmVpnPrefix::ErmVpnPrefix(const BgpProtoPrefix &prefix) {
    assert(IsValidForBgp(prefix.type));

    size_t rd_size = RouteDistinguisher::kSize;
    size_t rtid_size = 4;

    type_ = prefix.type;

    size_t rd_offset = 0;
    rd_ = RouteDistinguisher(&prefix.prefix[rd_offset]);

    size_t rtid_offset = rd_offset + rd_size;
    router_id_ = Ip4Address(get_value(&prefix.prefix[rtid_offset], rtid_size));

    size_t source_offset = rtid_offset + rtid_size + 1;
    source_ = Ip4Address(get_value(&prefix.prefix[source_offset], 4));

    size_t group_offset = source_offset + 4 + 1;
    group_ = Ip4Address(get_value(&prefix.prefix[group_offset], 4));
}

void ErmVpnPrefix::BuildProtoPrefix(BgpProtoPrefix *prefix) const {
    assert(IsValidForBgp(type_));

    size_t rd_size = RouteDistinguisher::kSize;
    size_t rtid_size = 4;

    prefix->prefixlen = (rd_size + rtid_size + 1 + 4 + 1 + 4) * 8;
    prefix->prefix.clear();
    prefix->type = type_;
    prefix->prefix.resize(prefix->prefixlen/8, 0);

    size_t rd_offset = 0;
    std::copy(rd_.GetData(), rd_.GetData() + rd_size,
        prefix->prefix.begin() + rd_offset);

    size_t rtid_offset = rd_offset + rd_size;
    const Ip4Address::bytes_type &rtid_bytes = router_id_.to_bytes();
    std::copy(rtid_bytes.begin(), rtid_bytes.begin() + 4,
        prefix->prefix.begin() + rtid_offset);

    size_t source_offset = rtid_offset + rtid_size + 1;
    prefix->prefix[source_offset - 1] = 32;
    const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
    std::copy(source_bytes.begin(), source_bytes.begin() + 4,
        prefix->prefix.begin() + source_offset);

    size_t group_offset = source_offset + 4 + 1;
    prefix->prefix[group_offset - 1] = 32;
    const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
    std::copy(group_bytes.begin(), group_bytes.begin() + 4,
              prefix->prefix.begin() + group_offset);
}

ErmVpnPrefix ErmVpnPrefix::FromString(const string &str,
    boost::system::error_code *errorp) {
    ErmVpnPrefix prefix, null_prefix;
    string temp_str;

    // Look for Type.
    size_t pos1 = str.find('-');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }
    temp_str = str.substr(0, pos1);
    stringToInteger(temp_str, prefix.type_);
    if (!IsValid(prefix.type_)) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }

    // Look for RD.
    size_t pos2 = str.find('-', pos1 + 1);
    if (pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }
    temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
    boost::system::error_code rd_err;
    prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
    if (rd_err != 0) {
        if (errorp != NULL) {
            *errorp = rd_err;
        }
        return null_prefix;
    }

    // Look for router-id.
    size_t pos3 = str.find(',', pos2 + 1);
    if (pos3 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }
    temp_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
    boost::system::error_code rtid_err;
    prefix.router_id_ = Ip4Address::from_string(temp_str, rtid_err);
    if (rtid_err != 0) {
        if (errorp != NULL) {
            *errorp = rtid_err;
        }
        return null_prefix;
    }

    // Look for group.
    size_t pos4 = str.find(',', pos3 + 1);
    if (pos4 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }
    temp_str = str.substr(pos3 + 1, pos4 - pos3 - 1);
    boost::system::error_code group_err;
    prefix.group_ = Ip4Address::from_string(temp_str, group_err);
    if (group_err != 0) {
        if (errorp != NULL) {
            *errorp = group_err;
        }
        return null_prefix;
    }

    // Rest is source.
    temp_str = str.substr(pos4 + 1, string::npos);
    boost::system::error_code source_err;
    prefix.source_ = Ip4Address::from_string(temp_str, source_err);
    if (source_err != 0) {
        if (errorp != NULL) {
            *errorp = source_err;
        }
        return null_prefix;
    }

    return prefix;
}

string ErmVpnPrefix::ToString() const {
    string repr = integerToString(type_);
    repr += "-" + rd_.ToString();
    repr += "-" + router_id_.to_string();
    repr += "," + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}

string ErmVpnPrefix::ToXmppIdString() const {
    assert(type_ == 0);
    string repr = rd_.ToString();
    repr += ":" + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}

bool ErmVpnPrefix::IsValidForBgp(uint8_t type) {
    return (type == LocalTreeRoute || type == GlobalTreeRoute);
}

bool ErmVpnPrefix::IsValid(uint8_t type) {
    return (type == NativeRoute || IsValidForBgp(type));
}

bool ErmVpnPrefix::operator==(const ErmVpnPrefix &rhs) const {
    return (
        type_ == rhs.type_ &&
        rd_ == rhs.rd_ &&
        router_id_ == rhs.router_id_ &&
        group_ == rhs.group_ &&
        source_ == rhs.source_);
}

ErmVpnRoute::ErmVpnRoute(const ErmVpnPrefix &prefix) : prefix_(prefix) {
}

int ErmVpnRoute::CompareTo(const Route &rhs) const {
    const ErmVpnRoute &other = static_cast<const ErmVpnRoute &>(rhs);
    KEY_COMPARE(prefix_.type(), other.prefix_.type());
    KEY_COMPARE(
        prefix_.route_distinguisher(), other.prefix_.route_distinguisher());
    KEY_COMPARE(prefix_.router_id(), other.prefix_.router_id());
    KEY_COMPARE(prefix_.source(), other.prefix_.source());
    KEY_COMPARE(prefix_.group(), other.prefix_.group());
    return 0;
}

string ErmVpnRoute::ToString() const {
    return prefix_.ToString();
}

string ErmVpnRoute::ToXmppIdString() const {
    return prefix_.ToXmppIdString();
}

bool ErmVpnRoute::IsValid() const {
    if (!BgpRoute::IsValid())
        return false;

    const BgpAttr *attr = BestPath()->GetAttr();
    switch (prefix_.type()) {
    case ErmVpnPrefix::NativeRoute:
        return attr->label_block();
        break;
    case ErmVpnPrefix::LocalTreeRoute:
        return attr->edge_discovery();
        break;
    case ErmVpnPrefix::GlobalTreeRoute:
        return attr->edge_forwarding();
        break;
    default:
        break;
    }

    return false;
}

void ErmVpnRoute::SetKey(const DBRequestKey *reqkey) {
    const ErmVpnTable::RequestKey *key =
        static_cast<const ErmVpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void ErmVpnRoute::BuildProtoPrefix(
    BgpProtoPrefix *prefix, uint32_t label) const {
    prefix_.BuildProtoPrefix(prefix);
}

void ErmVpnRoute::BuildBgpProtoNextHop(
    std::vector<uint8_t> &nh, IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}

DBEntryBase::KeyPtr ErmVpnRoute::GetDBRequestKey() const {
    ErmVpnTable::RequestKey *key;
    key = new ErmVpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
