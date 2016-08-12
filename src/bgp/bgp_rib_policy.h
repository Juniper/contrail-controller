/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_RIB_POLICY_H_
#define SRC_BGP_BGP_RIB_POLICY_H_

#include "bgp/bgp_proto.h"

//
// This class represents the export policy for a rib. Given that we do not
// currently support any real policy configuration, this is pretty trivial
// for now.
//
// Including the AS number as part of the policy results in creation of a
// different RibOut for every neighbor AS that we peer with. This allows a
// simplified implementation of the sender side AS path loop check. In most
// practical deployment scenarios all eBGP peers will belong to the same
// neighbor AS anyway.
//
// Including AS override as part of the policy results in creation of a
// different RibOuts for neighbors that need and don't AS override.
//
// Including the nexthop as part of the policy results in creation of a
// different RibOut for each set of BGPaaS clients that belong to the same
// subnet. This allows us to rewrite the nexthop to the specified value.
//
// Including the CPU affinity as part of the RibExportPolicy allows us to
// artificially create more RibOuts than otherwise necessary. This is used
// to achieve higher concurrency at the expense of creating more state.
//
// Including llgr as part of the policy results in the creation of a different
// ribout for each set of peers that do (or do not) support long lived
// graceful restart functionality. In order to ensure correctness, we need
// to not advertise LLGR_STALE community to peers who do not support LLGR.
// Instead, we should bring down the local pref to make the paths less
// preferable.
//
struct RibExportPolicy {
    enum Encoding {
        BGP,
        XMPP,
    };

    struct RemovePrivatePolicy {
        RemovePrivatePolicy();

        bool enabled;
        bool all;
        bool replace;
        bool peer_loop_check;
    };

    RibExportPolicy();
    RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
        int affinity, u_int32_t cluster_id);
    RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
        as_t as_number, bool as_override, bool llgr, int affinity,
        u_int32_t cluster_id);
    RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
        as_t as_number, bool as_override, bool llgr, IpAddress nexthop,
        int affinity, u_int32_t cluster_id);

    void SetRemovePrivatePolicy(bool all, bool replace, bool peer_loop_check);
    bool operator<(const RibExportPolicy &rhs) const;

    BgpProto::BgpPeerType type;
    Encoding encoding;
    as_t as_number;
    bool as_override;
    IpAddress nexthop;
    int affinity;
    bool llgr;
    uint32_t cluster_id;
    RemovePrivatePolicy remove_private;
};

#endif  // SRC_BGP_BGP_RIB_POLICY_H_
