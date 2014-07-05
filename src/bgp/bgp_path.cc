/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_path.h"
#include "bgp/bgp_server.h"

std::string BgpPath::PathIdString(uint32_t path_id) {
    Ip4Address addr(path_id);
    return addr.to_string();
}

std::string BgpPath::PathSourceString(PathSource source) {
    switch (source) {
        case None:
            return "None";
        case BGP_XMPP:
            return "BGP_XMPP";
        case StaticRoute:
            return "StaticRoute";
        case ServiceChain:
            return "SericeChain";
        case Local:
            return "Local";
        default:
            break;
    }
    return "Other";
}

BgpPath::BgpPath(const IPeer *peer, uint32_t path_id, PathSource src, 
                 const BgpAttrPtr ptr, uint32_t flags, uint32_t label)
    : peer_(peer), path_id_(path_id), source_(src), attr_(ptr), 
      flags_(flags), label_(label) {
}

BgpPath::BgpPath(const IPeer *peer, PathSource src, const BgpAttrPtr ptr, 
        uint32_t flags, uint32_t label)
    : peer_(peer), path_id_(0), source_(src), attr_(ptr), 
      flags_(flags), label_(label) {
}

BgpPath::BgpPath(uint32_t path_id, PathSource src, const BgpAttrPtr ptr,
        uint32_t flags, uint32_t label)
    : peer_(NULL), path_id_(path_id), source_(src), attr_(ptr), 
      flags_(flags), label_(label) {
}

// True is better
#define BOOL_COMPARE(CondA, CondB)   \
    do {                                \
        if (CondA) {                    \
            if (!(CondB)) return -1;    \
        } else {                        \
            if (CondB) return 1;        \
        }                               \
    } while (0)

int BgpPath::PathCompare(const BgpPath &rhs, bool allow_ecmp) const {
    const BgpAttr *rattr = rhs.GetAttr();

    // Feasible Path first
    KEY_COMPARE(rhs.IsFeasible(), IsFeasible());

    // Compare local_pref larger value is better, so compare in reverse order
    KEY_COMPARE(rattr->local_pref(), attr_->local_pref());

    //
    // For ECMP paths, above checks should suffice
    // TODO: Move this to add or relax other checks as appropriate
    //
    if (allow_ecmp) return 0;

    KEY_COMPARE(attr_->as_path_count(), rattr->as_path_count());

    KEY_COMPARE(attr_->origin(), rattr->origin());

    if (attr_->neighbor_as() == rattr->neighbor_as()) {
        KEY_COMPARE(attr_->med(), rattr->med());
    }

    // Prefer locally generated routes over bgp and xmpp routes.
    BOOL_COMPARE(peer_ == NULL, rhs.peer_ == NULL);

    // Compare the source and the path id.
    KEY_COMPARE(rhs.GetSource(), GetSource());

    // Bail if both paths are local since all subsequent checks are
    // based on IPeer properties.
    if (peer_ == NULL && rhs.peer_ == NULL) {
        KEY_COMPARE(path_id_, rhs.path_id_);
        return 0;
    }

    // Prefer xmpp routes over bgp routes.
    BOOL_COMPARE(peer_->IsXmppPeer(), rhs.peer_->IsXmppPeer());

    KEY_COMPARE(path_id_, rhs.path_id_);

    // Path received from EBGP is better than the one received from IBGP
    KEY_COMPARE(peer_->PeerType() == BgpProto::IBGP,
                rhs.peer_->PeerType() == BgpProto::IBGP);
    KEY_COMPARE(peer_->bgp_identifier(), rhs.peer_->bgp_identifier());

    const BgpPeer *lpeer = dynamic_cast<const BgpPeer *>(peer_);
    const BgpPeer *rpeer = dynamic_cast<const BgpPeer *>(rhs.peer_);
    if (lpeer != NULL && rpeer != NULL) {
        KEY_COMPARE(lpeer->peer_key(), rpeer->peer_key());
    }

    return 0;
}

BgpSecondaryPath::BgpSecondaryPath(const IPeer *peer, uint32_t path_id,
        PathSource src, const BgpAttrPtr ptr, uint32_t flags, uint32_t label)
    : BgpPath(peer, path_id, src, ptr, flags, label) {
}
