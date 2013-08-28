/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/community.h"

#include "bgp/bgp_proto.h"

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

int Community::CompareTo(const Community &rhs) const {
    KEY_COMPARE(communities_, rhs.communities_);
    return 0;
}

void Community::Remove() {
    comm_db_->Delete(this);
}

CommunityDB::CommunityDB(BgpServer *server) : server_(server) {
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

void ExtCommunity::Append(const ExtCommunityList &export_rt) {
    communities_.insert(communities_.end(), export_rt.begin(), export_rt.end());
    return;
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
    return;
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
    return;
}

ExtCommunity::ExtCommunity(ExtCommunityDB *extcomm_db,
        const ExtCommunitySpec spec) : extcomm_db_(extcomm_db) {
    refcount_ = 0;
    std::vector<uint64_t>::const_iterator it = spec.communities.begin();
    for (; it < spec.communities.end(); it++) {
        ExtCommunityValue comm;
        put_value(comm.data(), comm.size(), *it);
        communities_.push_back(comm);
    }
}

ExtCommunityDB::ExtCommunityDB(BgpServer *server) : server_(server) {
}

ExtCommunityPtr ExtCommunityDB::AppendAndLocate(const ExtCommunity *src, 
                        const ExtCommunity::ExtCommunityList &export_list) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->Append(export_list);
    return Locate(clone);
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
