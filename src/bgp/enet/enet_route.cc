/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/enet/enet_route.h"
#include "bgp/enet/enet_table.h"

using namespace std;
using boost::system::error_code;

EnetPrefix::EnetPrefix(const BgpProtoPrefix &prefix) {
}

EnetPrefix EnetPrefix::FromString(const string &str, error_code *errorp) {
    EnetPrefix prefix;

    size_t pos1 = str.find(',');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    string mac_str = str.substr(0, pos1);
    error_code mac_err;
    prefix.mac_addr_ = MacAddress::FromString(mac_str, &mac_err);
    if (mac_err != 0) {
        if (errorp != NULL) {
            *errorp = mac_err;
        }
        return prefix;
    }

    string ip_str = str.substr(pos1 + 1, string::npos);
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

string EnetPrefix::ToString() const {
    string str = mac_addr_.ToString() + "," + ip_prefix_.ToString();
    return str;
}

EnetRoute::EnetRoute(const EnetPrefix &prefix)
    : prefix_(prefix) {
}

int EnetRoute::CompareTo(const Route &rhs) const {
    const EnetRoute &other = static_cast<const EnetRoute &>(rhs);
    KEY_COMPARE(prefix_.mac_addr(), other.prefix_.mac_addr());
    KEY_COMPARE(prefix_.ip_prefix(), other.prefix_.ip_prefix());

    return 0;
}

string EnetRoute::ToString() const {
    return prefix_.ToString();
}

void EnetRoute::SetKey(const DBRequestKey *reqkey) {
    const EnetTable::RequestKey *key =
        static_cast<const EnetTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void EnetRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
        uint32_t label) const {
}

void EnetRoute::BuildBgpProtoNextHop(vector<uint8_t> &nh,
        IpAddress nexthop) const {
}

DBEntryBase::KeyPtr EnetRoute::GetDBRequestKey() const {
    EnetTable::RequestKey *key =
        new EnetTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
