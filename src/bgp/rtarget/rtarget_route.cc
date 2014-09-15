/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_route.h"
#include "bgp/rtarget/rtarget_table.h"

using namespace std;

RTargetRoute::RTargetRoute(const RTargetPrefix &prefix)
    : prefix_(prefix) {
}

int RTargetRoute::CompareTo(const Route &rhs) const {
    const RTargetRoute &rt_rhs = static_cast<const RTargetRoute &>(rhs);
    return prefix_.CompareTo(rt_rhs.prefix_);
}

string RTargetRoute::ToString() const {
    return prefix_.ToString();
}

void RTargetRoute::SetKey(const DBRequestKey *reqkey) {
    const RTargetTable::RequestKey *key =
        static_cast<const RTargetTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void RTargetRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
                                    const BgpAttr *attr,
                                    uint32_t label) const {
    prefix_.BuildProtoPrefix(prefix);
}

void RTargetRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &nh, 
                                        IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}

DBEntryBase::KeyPtr RTargetRoute::GetDBRequestKey() const {
    RTargetTable::RequestKey *key = 
        new RTargetTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
