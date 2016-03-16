/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_rib_policy.h"

RibExportPolicy::RibExportPolicy()
    : type(BgpProto::IBGP),
      encoding(BGP),
      as_number(0),
      as_override(false),
      affinity(-1),
      cluster_id(0) {
}

RibExportPolicy::RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
    int affinity, u_int32_t cluster_id)
    : type(type),
      encoding(encoding),
      as_number(0),
      as_override(false),
      affinity(affinity),
      cluster_id(cluster_id) {
    if (encoding == XMPP)
        assert(type == BgpProto::XMPP);
    if (encoding == BGP)
        assert(type == BgpProto::IBGP || type == BgpProto::EBGP);
}

RibExportPolicy::RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
    as_t as_number, bool as_override, int affinity, u_int32_t cluster_id)
    : type(type),
      encoding(encoding),
      as_number(as_number),
      as_override(as_override),
      affinity(affinity),
      cluster_id(cluster_id) {
    if (encoding == XMPP)
        assert(type == BgpProto::XMPP);
    if (encoding == BGP)
        assert(type == BgpProto::IBGP || type == BgpProto::EBGP);
}

RibExportPolicy::RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
    as_t as_number, bool as_override, IpAddress nexthop, int affinity,
    u_int32_t cluster_id)
    : type(type),
      encoding(BGP),
      as_number(as_number),
      as_override(as_override),
      nexthop(nexthop),
      affinity(affinity),
      cluster_id(cluster_id) {
    assert(type == BgpProto::IBGP || type == BgpProto::EBGP);
    assert(encoding == BGP);
}

//
// Implement operator< for RibExportPolicy by comparing each of the fields.
//
bool RibExportPolicy::operator<(const RibExportPolicy &rhs) const {
    BOOL_KEY_COMPARE(encoding, rhs.encoding);
    BOOL_KEY_COMPARE(type, rhs.type);
    BOOL_KEY_COMPARE(as_number, rhs.as_number);
    BOOL_KEY_COMPARE(as_override, rhs.as_override);
    BOOL_KEY_COMPARE(nexthop, rhs.nexthop);
    BOOL_KEY_COMPARE(affinity, rhs.affinity);
    BOOL_KEY_COMPARE(cluster_id, rhs.cluster_id);
    return false;
}
