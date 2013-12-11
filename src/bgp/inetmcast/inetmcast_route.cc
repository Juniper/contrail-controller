/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmcast/inetmcast_route.h"
#include "bgp/inetmcast/inetmcast_table.h"

using namespace std;

InetMcastPrefix::InetMcastPrefix() {
}

InetMcastPrefix InetMcastPrefix::FromString(const string &str,
        boost::system::error_code *errorp) {
    InetMcastPrefix prefix;

    size_t pos1 = str.rfind(':');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    string rd_str = str.substr(0, pos1);
    boost::system::error_code rd_err;
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
    string group_str = str.substr(pos1 + 1, pos2 - pos1 -1);
    boost::system::error_code group_err;
    prefix.group_ = Ip4Address::from_string(group_str, group_err);
    if (group_err != 0) {
        if (errorp != NULL) {
            *errorp = group_err;
        }
        return prefix;
    }

    string source_str = str.substr(pos2 + 1, string::npos);
    boost::system::error_code source_err;
    prefix.source_ = Ip4Address::from_string(source_str, source_err);
    if (source_err != 0) {
        if (errorp != NULL) {
            *errorp = source_err;
        }
        return prefix;
    }

    return prefix;
}

string InetMcastPrefix::ToString() const {
    string repr = rd_.ToString();
    repr += ":" + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}

InetMcastRoute::InetMcastRoute(const InetMcastPrefix &prefix)
    : prefix_(prefix) {
}

int InetMcastRoute::CompareTo(const Route &rhs) const {
    const InetMcastRoute &other = static_cast<const InetMcastRoute &>(rhs);
    int res = prefix_.route_distinguisher().CompareTo(
        other.prefix_.route_distinguisher());
    if (res != 0) {
        return res;
    }

    Ip4Address lgroup = prefix_.group();
    Ip4Address rgroup = other.prefix_.group();
    if (lgroup < rgroup) {
        return -1;
    }
    if (lgroup > rgroup) {
        return 1;
    }

    Ip4Address lsource = prefix_.source();
    Ip4Address rsource = other.prefix_.source();
    if (lsource < rsource) {
        return -1;
    }
    if (lsource > rsource) {
        return 1;
    }

    return 0;
}

string InetMcastRoute::ToString() const {
    return prefix_.ToString();
}

void InetMcastRoute::SetKey(const DBRequestKey *reqkey) {
    const InetMcastTable::RequestKey *key =
        static_cast<const InetMcastTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void InetMcastRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
        uint32_t label) const {
}

void InetMcastRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
        IpAddress nexthop) const {
}

DBEntryBase::KeyPtr InetMcastRoute::GetDBRequestKey() const {
    InetMcastTable::RequestKey *key =
        new InetMcastTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
