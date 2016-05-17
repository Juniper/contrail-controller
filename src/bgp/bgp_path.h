/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PATH_H_
#define SRC_BGP_BGP_PATH_H_

#include <string>

#include "base/util.h"
#include "route/path.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_attr.h"

class BgpTable;
class BgpRoute;

class BgpPath : public Path {
public:
    enum PathFlag {
        AsPathLooped = 1 << 0,
        NoNeighborAs = 1 << 1,
        Stale = 1 << 2,
        NoTunnelEncap = 1 << 3,
        OriginatorIdLooped = 1 << 4,
    };

    // Ordered in the ascending order of path preference
    enum PathSource {
        None = 0,
        BGP_XMPP = 1,
        ServiceChain = 2,
        StaticRoute = 3,
        Local = 4,
    };

    static const uint32_t INFEASIBLE_MASK =
        (AsPathLooped|NoNeighborAs|NoTunnelEncap|OriginatorIdLooped);

    static std::string PathIdString(uint32_t path_id);
    static std::string PathSourceString(PathSource source);

    BgpPath(const IPeer *peer, uint32_t path_id, PathSource src,
            const BgpAttrPtr ptr, uint32_t flags, uint32_t label);
    BgpPath(const IPeer *peer, PathSource src, const BgpAttrPtr attr,
            uint32_t flags, uint32_t label);
    BgpPath(uint32_t path_id, PathSource src, const BgpAttrPtr attr,
            uint32_t flags = 0, uint32_t label = 0);
    BgpPath(PathSource src, const BgpAttrPtr attr,
            uint32_t flags = 0, uint32_t label = 0);
    virtual ~BgpPath() {
    }


    bool IsVrfOriginated() const {
        if (IsReplicated())
            return false;
        if (source_ != BGP_XMPP && source_ != Local)
            return false;
        return true;
    }

    const IPeer *GetPeer() const {
        return peer_;
    }

    const uint32_t GetPathId() const {
        return path_id_;
    }

    void UpdatePeerRefCount(int count) {
        if (!peer_)
            return;
        peer_->UpdateRefCount(count);
        if (source_ != BGP_XMPP || IsReplicated())
            return;
        peer_->UpdatePrimaryPathCount(count);
    }

    const BgpAttr *GetAttr() const {
        return attr_.get();
    }

    uint32_t GetLabel() const {
        return label_;
    }

    virtual bool IsReplicated() const {
        return false;
    }

    bool IsFeasible() const {
        return ((flags_ & INFEASIBLE_MASK) == 0);
    }

    uint32_t GetFlags() const {
        return flags_;
    }

    PathSource GetSource() const {
        return source_;
    }

    // Check if the path is stale
    bool IsStale() const {
        return ((flags_ & Stale) != 0);
    }

    // Mark a path as stale
    void SetStale() {
        flags_ |= Stale;
    }

    // Reset a path as active (not stale)
    void ResetStale() {
        flags_ &= ~Stale;
    }

    virtual std::string ToString() const {
        // Dump the peer name
        return peer_ ? peer_->ToString() : "Nil";
    }

    // Select one path over other
    int PathCompare(const BgpPath &rhs, bool allow_ecmp) const;
    bool PathSameNeighborAs(const BgpPath &rhs) const;

private:
    const IPeer *peer_;
    const uint32_t path_id_;
    const PathSource source_;
    const BgpAttrPtr attr_;
    uint32_t flags_;
    uint32_t label_;
};

class BgpSecondaryPath : public BgpPath {
public:
    BgpSecondaryPath(const IPeer *peer, uint32_t path_id, PathSource src,
                     const BgpAttrPtr attr, uint32_t flags, uint32_t label);

    virtual bool IsReplicated() const {
        return true;
    }

    void SetReplicateInfo(const BgpTable *table, const BgpRoute *rt) {
        src_table_ = table;
        src_entry_ = rt;
    }

    virtual ~BgpSecondaryPath() {
    }

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
