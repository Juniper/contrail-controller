/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/community.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <string>

#include "base/string_util.h"
#include "bgp/bgp_proto.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/origin-vn/origin_vn.h"
#include "net/community_type.h"

using std::sort;
using std::string;
using std::unique;
using std::vector;

void CommunitySpec::ToCanonical(BgpAttr *attr) {
    attr->set_community(this);
}

string CommunitySpec::ToString() const {
    string repr;
    char start[32];
    snprintf(start, sizeof(start), "Communities: %zu [", communities.size());
    repr += start;

    for (size_t i = 0; i < communities.size(); ++i) {
        char community[12];
        snprintf(community, sizeof(community), " %X", communities[i]);
        repr += community;
    }
    repr += " ]";

    return repr;
}

Community::Community(CommunityDB *comm_db, const CommunitySpec spec)
    : comm_db_(comm_db), communities_(spec.communities) {
    refcount_ = 0;
    sort(communities_.begin(), communities_.end());
    vector<uint32_t>::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

int Community::CompareTo(const Community &rhs) const {
    KEY_COMPARE(communities_, rhs.communities_);
    return 0;
}

size_t CommunitySpec::EncodeLength() const {
    return communities.size() * sizeof(uint32_t);
}

void Community::Append(uint32_t value) {
    if (ContainsValue(value))
        return;
    communities_.push_back(value);
    sort(communities_.begin(), communities_.end());
}

void Community::Append(const std::vector<uint32_t> &communities) {
    BOOST_FOREACH(uint32_t community, communities) {
        communities_.push_back(community);
    }
    sort(communities_.begin(), communities_.end());
    vector<uint32_t>::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

void Community::Set(const std::vector<uint32_t> &communities) {
    communities_.clear();
    BOOST_FOREACH(uint32_t community, communities) {
        communities_.push_back(community);
    }
}

void Community::Remove(const std::vector<uint32_t> &communities) {
    BOOST_FOREACH(uint32_t community, communities) {
        communities_.erase(
               std::remove(communities_.begin(), communities_.end(), community),
               communities_.end());
    }
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

void Community::BuildStringList(vector<string> *list) const {
    BOOST_FOREACH(uint32_t community, communities_) {
        string name = CommunityType::CommunityToString(community);
        list->push_back(name);
    }
}

CommunityDB::CommunityDB(BgpServer *server) {
}

CommunityPtr CommunityDB::AppendAndLocate(const Community *src,
    uint32_t value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Append(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::AppendAndLocate(const Community *src,
    const std::vector<uint32_t> &value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Append(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::SetAndLocate(const Community *src,
    const std::vector<uint32_t> &value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Set(value);
    return Locate(clone);
}

CommunityPtr CommunityDB::RemoveAndLocate(const Community *src,
    const std::vector<uint32_t> &value) {
    Community *clone;
    if (src) {
        clone = new Community(*src);
    } else {
        clone = new Community(this);
    }

    clone->Remove(value);
    return Locate(clone);
}

string ExtCommunitySpec::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "ExtCommunity <code: %d, flags: %02x>:%d",
             code, flags, (uint32_t)communities.size());
    return string(repr);
}

size_t ExtCommunitySpec::EncodeLength() const {
    return communities.size() * sizeof(uint64_t);
}

int ExtCommunitySpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(communities,
        static_cast<const ExtCommunitySpec &>(rhs_attr).communities);
    return 0;
}

void ExtCommunitySpec::ToCanonical(BgpAttr *attr) {
    attr->set_ext_community(this);
}

int ExtCommunity::CompareTo(const ExtCommunity &rhs) const {
    KEY_COMPARE(communities_.size(), rhs.communities_.size());

    ExtCommunityList::const_iterator i, j;
    for (i = communities_.begin(), j = rhs.communities_.begin();
         i < communities_.end(); ++i, ++j) {
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
    sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());
}

void ExtCommunity::Append(const ExtCommunityValue &value) {
    communities_.push_back(value);
    sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        unique(communities_.begin(), communities_.end());
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
            ++it;
        }
    }
}

void ExtCommunity::RemoveSGID() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_security_group(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveSiteOfOrigin() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_site_of_origin(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveOriginVn() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_origin_vn(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveTunnelEncapsulation() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_tunnel_encap(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExtCommunity::RemoveLoadBalance() {
    for (ExtCommunityList::iterator it = communities_.begin();
         it != communities_.end(); ) {
        if (ExtCommunity::is_load_balance(*it)) {
            it = communities_.erase(it);
        } else {
            ++it;
        }
    }
}

vector<string> ExtCommunity::GetTunnelEncap() const {
    vector<string> encap_list;
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tunnel_encap(*iter))
            continue;
        TunnelEncap encap(*iter);
        if (encap.tunnel_encap() == TunnelEncapType::UNSPEC)
            continue;
        encap_list.push_back(encap.ToXmppString());
    }

    sort(encap_list.begin(), encap_list.end());
    vector<string>::iterator encap_iter =
        unique(encap_list.begin(), encap_list.end());
    encap_list.erase(encap_iter, encap_list.end());
    return encap_list;
}

bool ExtCommunity::ContainsTunnelEncapVxlan() const {
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_tunnel_encap(*iter))
            continue;
        TunnelEncap encap(*iter);
        if (encap.tunnel_encap() == TunnelEncapType::VXLAN)
            return true;
        if (encap.tunnel_encap() == TunnelEncapType::VXLAN_CONTRAIL)
            return true;
    }
    return false;
}

int ExtCommunity::GetOriginVnIndex() const {
    for (ExtCommunityList::const_iterator iter = communities_.begin();
         iter != communities_.end(); ++iter) {
        if (!ExtCommunity::is_origin_vn(*iter))
            continue;
        OriginVn origin_vn(*iter);
        return origin_vn.vn_index();
    }
    return -1;
}

ExtCommunity::ExtCommunity(ExtCommunityDB *extcomm_db,
        const ExtCommunitySpec spec) : extcomm_db_(extcomm_db) {
    refcount_ = 0;
    for (vector<uint64_t>::const_iterator it = spec.communities.begin();
         it < spec.communities.end(); ++it) {
        ExtCommunityValue comm;
        put_value(comm.data(), comm.size(), *it);
        communities_.push_back(comm);
    }
    sort(communities_.begin(), communities_.end());
    ExtCommunityList::iterator it =
        unique(communities_.begin(), communities_.end());
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

ExtCommunityPtr ExtCommunityDB::ReplaceSGIDListAndLocate(
    const ExtCommunity *src,
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

ExtCommunityPtr ExtCommunityDB::RemoveSiteOfOriginAndLocate(
        const ExtCommunity *src) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSiteOfOrigin();
    return Locate(clone);
}

ExtCommunityPtr ExtCommunityDB::ReplaceSiteOfOriginAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &soo) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveSiteOfOrigin();
    clone->Append(soo);
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

ExtCommunityPtr ExtCommunityDB::ReplaceLoadBalanceAndLocate(
        const ExtCommunity *src,
        const ExtCommunity::ExtCommunityValue &lb) {
    ExtCommunity *clone;
    if (src) {
        clone = new ExtCommunity(*src);
    } else {
        clone = new ExtCommunity(this);
    }

    clone->RemoveLoadBalance();
    clone->Append(lb);
    return Locate(clone);
}
