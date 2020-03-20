/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PATH_H_
#define SRC_BGP_BGP_PATH_H_

#include <string>
#include <vector>

#include "base/util.h"
#include "route/path.h"
#include "bgp/bgp_attr.h"

class BgpTable;
class BgpRoute;
class IPeer;

class BgpPath : public Path {
public:
    enum PathFlag {
        AsPathLooped = 1 << 0,
        NoNeighborAs = 1 << 1,
        Stale = 1 << 2,
        NoTunnelEncap = 1 << 3,
        OriginatorIdLooped = 1 << 4,
        ResolveNexthop = 1 << 5,
        ResolvedPath = 1 << 6,
        RoutingPolicyReject = 1 << 7,
        LlgrStale = 1 << 8,
        ClusterListLooped = 1 << 9,
        AliasedPath = 1 << 10,
        CheckGlobalErmVpnRoute = 1 << 11,
    };

    // Ordered in the ascending order of path preference
    enum PathSource {
        None = 0,
        BGP_XMPP = 1,
        ServiceChain = 2,
        StaticRoute = 3,
        Aggregate = 4,
        Local = 5
    };

    static const uint32_t INFEASIBLE_MASK = (AsPathLooped |
        NoNeighborAs | NoTunnelEncap | OriginatorIdLooped | ResolveNexthop |
        RoutingPolicyReject | ClusterListLooped | CheckGlobalErmVpnRoute);

    static std::string PathIdString(uint32_t path_id);

    BgpPath(const IPeer *peer, uint32_t path_id, PathSource src,
            const BgpAttrPtr ptr, uint32_t flags, uint32_t label,
            uint32_t l3_label = 0);
    BgpPath(const IPeer *peer, PathSource src, const BgpAttrPtr attr,
            uint32_t flags, uint32_t label, uint32_t l3_label = 0);
    BgpPath(uint32_t path_id, PathSource src, const BgpAttrPtr attr,
            uint32_t flags = 0, uint32_t label = 0, uint32_t l3_label = 0);
    BgpPath(PathSource src, const BgpAttrPtr attr,
            uint32_t flags = 0, uint32_t label = 0, uint32_t l3_label = 0);
    virtual ~BgpPath() {
    }

    void AddExtCommunitySubCluster(uint32_t subcluster_id);

    RouteDistinguisher GetSourceRouteDistinguisher() const;

    bool IsVrfOriginated() const {
        if (IsReplicated())
            return false;
        if (source_ != Aggregate && source_ != BGP_XMPP && source_ != Local)
            return false;
        return true;
    }

    IPeer *GetPeer() { return const_cast<IPeer *>(peer_); }
    const IPeer *GetPeer() const { return peer_; }
    const uint32_t GetPathId() const { return path_id_; }

    void UpdatePeerRefCount(int count, Address::Family family) const;

    void SetAttr(const BgpAttrPtr attr, const BgpAttrPtr original_attr) {
        attr_ = attr;
        original_attr_ = original_attr;
    }

    const BgpAttr *GetAttr() const { return attr_.get(); }
    const BgpAttr *GetOriginalAttr() const { return original_attr_.get(); }
    uint32_t GetLabel() const { return label_; }
    uint32_t GetL3Label() const { return l3_label_; }
    virtual bool IsReplicated() const { return false; }
    bool IsFeasible() const { return ((flags_ & INFEASIBLE_MASK) == 0); }

    bool IsResolutionFeasible() const {
        return ((flags_ & (INFEASIBLE_MASK & ~ResolveNexthop)) == 0);
    }

    bool IsAliased() const { return ((flags_ & AliasedPath) != 0); }
    bool IsResolved() const { return ((flags_ & ResolvedPath) != 0); }
    uint32_t GetFlags() const { return flags_; }
    std::vector<std::string> GetFlagsStringList() const;

    PathSource GetSource() const { return source_; }
    std::string GetSourceString(bool combine_bgp_and_xmpp = false) const;

    // Check if the path is stale
    bool IsStale() const { return ((flags_ & Stale) != 0); }

    // Check if the path is stale
    bool IsLlgrStale() const { return ((flags_ & LlgrStale) != 0); }

    // Mark a path as rejected by Routing policy
    void SetPolicyReject() { flags_ |= RoutingPolicyReject; }

    // Reset a path as active from Routing Policy
    void ResetPolicyReject() { flags_ &= ~RoutingPolicyReject; }

    bool IsPolicyReject() const {
        return ((flags_ & RoutingPolicyReject) != 0);
    }

    // Mark a path as stale
    void SetStale() { flags_ |= Stale; }

    // Reset a path as active (not stale)
    void ResetStale() { flags_ &= ~Stale; }

    void SetLlgrStale() { flags_ |= LlgrStale; }
    void ResetLlgrStale() { flags_ &= ~LlgrStale; }

    bool NeedsResolution() const { return ((flags_ & ResolveNexthop) != 0); }
    bool CheckErmVpn() const {
        return ((flags_ & CheckGlobalErmVpnRoute) != 0);
    }
    void ResetCheckErmVpn() { flags_ &= ~CheckGlobalErmVpnRoute; }
    void SetCheckErmVpn() { flags_ |= CheckGlobalErmVpnRoute; }

    virtual std::string ToString() const;

    // Select one path over other
    int PathCompare(const BgpPath &rhs, bool allow_ecmp) const;
    bool PathSameNeighborAs(const BgpPath &rhs) const;

private:
    const IPeer *peer_;
    const uint32_t path_id_;
    const PathSource source_;
    // Attribute for the BgpPath. If routing policy updates the path attribute,
    // this member contains the attribute after policy update
    BgpAttrPtr attr_;
    // Original path attribute before applying routing policy
    BgpAttrPtr original_attr_;
    uint32_t flags_;
    uint32_t label_;
    uint32_t l3_label_;
};

class BgpSecondaryPath : public BgpPath {
public:
    BgpSecondaryPath(const IPeer *peer, uint32_t path_id, PathSource src,
                     const BgpAttrPtr attr, uint32_t flags, uint32_t label,
                     uint32_t l3_label = 0);

    virtual bool IsReplicated() const {
        return true;
    }

    void SetReplicateInfo(const BgpTable *table, const BgpRoute *rt) {
        src_table_ = table;
        src_entry_ = rt;
    }

    virtual ~BgpSecondaryPath() {
    }

    RouteDistinguisher GetPrimaryRouteDistinguisher() const;

    const BgpTable *src_table() const {
        return src_table_;
    }

    const BgpRoute *src_rt() const {
        return src_entry_;
    }

private:
    const BgpTable *src_table_;
    const BgpRoute *src_entry_;

    DISALLOW_COPY_AND_ASSIGN(BgpSecondaryPath);
};

#endif  // SRC_BGP_BGP_PATH_H_
