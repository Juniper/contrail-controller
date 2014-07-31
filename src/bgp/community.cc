/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/community.h"

#include "bgp/bgp_proto.h"
#include "bgp/bgp_proto.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

void CommunitySpec::ToCanonical(BgpAttr *attr) {
    attr->set_community(this);
}

std::string CommunitySpec::ToString() const {
    char repr[1024];
    snprintf(repr, sizeof(repr),
             "Communities: %d [", (uint32_t)communities.size());

    for (size_t i = 0; i < communities.size(); i++) {
        char community[12];
        snprintf(community, sizeof(community),
                 " %X", (uint32_t)communities[i]);
        strcat(repr, community);
    }
    char end[3];
    snprintf(end, sizeof(end), " ]");
    strcat(repr, end);

    return std::string(repr);
}

Community::Community(CommunityDB *comm_db, const CommunitySpec spec)
    : comm_db_(comm_db), communities_(spec.communities) {
    refcount_ = 0;
    std::sort(communities_.begin(), communities_.end());
    std::vector<uint32_t>::iterator it =
        std::unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

int Community::CompareTo(const Community &rhs) const {
    KEY_COMPARE(communities_, rhs.communities_);
    return 0;
}

void Community::Remove() {
    comm_db_->Delete(this);
}

bool Community::ContainsValue(uint32_t value) const {
    BOOST_FOREACH(uint32_t community, communities_) {
        if (community == value)
            return true;
    }
    return false;
}

CommunityDB::CommunityDB(BgpServer *server) {
}

std::string ExtCommunitySpec::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "ExtCommunity <code: %d, flags: %02x>:%d",
             code, flags, (uint32_t)communities.size());
    return std::string(repr);
}

void ExtCommunitySpec::ToCanonical(BgpAttr *attr) {
    attr->set_ext_community(this);
}

int ExtCommunity::CompareTo(const ExtCommunity &rhs) const {
    KEY_COMPARE(communities_.size(), rhs.communities_.size());

    ExtCommunityList::const_iterator i, j;
    for (i = communities_.begin(), j = rhs.communities_.begin();
            i < communities_.end(); i++, j++) {
        if (*i < *j) {
            return -1;
        }
        if (*i > *j) {
            return 1;
        }
    }
    return 0;
}

void ExtCommunity::Remove() {
    extcomm_db_->Delete(this);
}

void ExtCommunity::Append(const ExtCommunityList &list) {
    communities_.insert(communities_.end(), list.begin(), list.end());
    std::sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        std::unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

void ExtCommunity::Append(const ExtCommunityValue &value) {
    communities_.push_back(value);
    std::sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        std::unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

bool ExtCommunity::ContainsOriginVn(const ExtCommunityValue &val) const {
    for (ExtCommunityList::const_iterator it = communities_.begin();
         it != communities_.end(); ++it) {
        if (ExtCommunity::is_origin_vn(*it) && *it == val)
            return true;
    }
    return false;
}

void ExtCommunity::RemoveRTarget() {
    for (ExtCommunityList::iterator it = communities_.begin(); 
         it != communities_.end(); ) {
        if (ExtCommunity::is_route_target(*it)) {
            it = communities_.erase(it);
        } else {
            it++;
        }
    }
}

void ExtCommunity::RemoveSGID() {
    for (ExtCommunityList::iterator it = communities_.begin(); 
         it != communities_.end(); ) {
        if (ExtCommunity::is_security_group(*it)) {
            it = communities_.erase(it);
        } else {
            it++;
        }
    }
}

void ExtCommunity::RemoveOriginVn() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_origin_vn(*it)) {
            it = communities_.erase(it);
        } else {
            it++;
        }
    }
}

void ExtCommunity::RemoveTunnelEncapsulation() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_tunnel_encap(*it)) {
            it = communities_.erase(it);
        } else {
            it++;
        }
    }
}

std::vector<std::string> ExtCommunity::GetTunnelEncap() const {
    std::vector<std::string> encap_list;
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tunnel_encap(*iter))
            continue;
        TunnelEncap encap(*iter);
        TunnelEncapType::Encap id = encap.tunnel_encap();
        if (id == TunnelEncapType::UNSPEC)
            continue;
        encap_list.push_back(TunnelEncapType::TunnelEncapToString(id));
    }

    std::sort(encap_list.begin(), encap_list.end());
    return encap_list;
}

ExtCommunity::ExtCommunity(ExtCommunityDB *extcomm_db,
        const ExtCommunitySpec spec) : extcomm_db_(extcomm_db) {
    refcount_ = 0;
    for (std::vector<uint64_t>::const_iterator it = spec.communities.begin();
         it < spec.communities.end(); it++) {
        ExtCommunityValue comm;
        put_value(comm.data(), comm.size(), *it);
        communities_.push_back(comm);
    }
    std::sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        std::unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

ExtCommunityDB::ExtCommunityDB(BgpServer *server) {
}

ExtCommunityPtr ExtCommunityDB::AppendAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->Append(list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::AppendAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &value) {
    ExtCommunity::ExtCommunityList list;
    list.push_back(value);
    return AppendAndLocate(src, list);
}

ExtCommunityPtr ExtCommunityDB::ReplaceRTargetAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &export_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveRTarget();
    clone->Append(export_list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceSGIDListAndLocate(const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &sgid_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSGID();
    clone->Append(sgid_list);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::RemoveOriginVnAndLocate(
        const ExtCommunity *src) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveOriginVn();
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceOriginVnAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &origin_vn) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveOriginVn();
    clone->Append(origin_vn);
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceTunnelEncapsulationAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityList &tunnel_encaps) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveTunnelEncapsulation();
    clone->Append(tunnel_encaps);
    return Locate(clone);
}
