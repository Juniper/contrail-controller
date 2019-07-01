/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy_action.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <sstream>

#include "bgp/bgp_attr.h"
#include "bgp/bgp_server.h"
#include "bgp/community.h"

using std::copy;
using std::ostringstream;
using std::string;
using std::vector;

UpdateAsPath::UpdateAsPath(const vector<uint32_t> &asn_list)
    : asn_list_(asn_list) {
}

void UpdateAsPath::operator()(BgpAttr *attr) const {
    if (!attr)
        return;
    BgpAttrDB *attr_db = attr->attr_db();
    BgpServer *server = attr_db->server();
    if (!server->enable_4byte_as()) {
        const AsPath *as_path = attr->as_path();
        if (as_path) {
            const AsPathSpec &as_path_spec = as_path->path();
            AsPathSpec *as_path_spec_ptr = as_path_spec.Add(asn_list_);
            attr->set_as_path(as_path_spec_ptr);
            delete as_path_spec_ptr;
        } else {
            AsPathSpec as_path_spec;
            AsPathSpec *as_path_spec_ptr = as_path_spec.Add(asn_list_);
            attr->set_as_path(as_path_spec_ptr);
            delete as_path_spec_ptr;
        }
    } else {
        const AsPath4Byte *as_path = attr->aspath_4byte();
        if (as_path) {
            const AsPath4ByteSpec &as_path_spec = as_path->path();
            AsPath4ByteSpec *as_path_spec_ptr = as_path_spec.Add(asn_list_);
            attr->set_aspath_4byte(as_path_spec_ptr);
            delete as_path_spec_ptr;
        } else {
            AsPath4ByteSpec as_path_spec;
            AsPath4ByteSpec *as_path_spec_ptr = as_path_spec.Add(asn_list_);
            attr->set_aspath_4byte(as_path_spec_ptr);
            delete as_path_spec_ptr;
        }
    }
}

string UpdateAsPath::ToString() const {
    ostringstream oss;
    oss << "as-path expand [ ";
    BOOST_FOREACH(uint32_t asn, asn_list_) {
        oss << asn << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

bool UpdateAsPath::IsEqual(const RoutingPolicyAction &as_path) const {
    const UpdateAsPath in_as_path = static_cast<const UpdateAsPath&>(as_path);
    return (asn_list_ == in_as_path.asn_list_);
}

UpdateCommunity::UpdateCommunity(const std::vector<string> communities,
                                 string op) {
    BOOST_FOREACH(const string &community, communities) {
        uint32_t value = CommunityType::CommunityFromString(community);
        if (value) communities_.push_back(value);
    }
    std::sort(communities_.begin(), communities_.end());
    std::vector<uint32_t>::iterator it =
        std::unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());

    if (strcmp(op.c_str(), "add") == 0) {
        op_ = ADD;
    } else if (strcmp(op.c_str(), "remove") == 0) {
        op_ = REMOVE;
    } else if (strcmp(op.c_str(), "set") == 0) {
        op_ = SET;
    }
}

void UpdateCommunity::operator()(BgpAttr *attr) const {
    if (!attr) return;
    const Community *comm = attr->community();
    BgpAttrDB *attr_db = attr->attr_db();
    BgpServer *server = attr_db->server();
    CommunityDB *comm_db = server->comm_db();
    CommunityPtr new_community = NULL;
    if (op_ == SET) {
        new_community = comm_db->SetAndLocate(comm, communities_);
    } else if (op_ == ADD) {
        new_community = comm_db->AppendAndLocate(comm, communities_);
    } else if (op_ == REMOVE) {
        if (comm) new_community = comm_db->RemoveAndLocate(comm, communities_);
    }
    attr->set_community(new_community);
}

string UpdateCommunity::ToString() const {
    ostringstream oss;
    if (op_ == SET) oss << "community set [ ";
    else if  (op_ == ADD) oss << "community add [ ";
    else if (op_ == REMOVE) oss << "community remove [ ";

    BOOST_FOREACH(uint32_t community, communities()) {
        string name = CommunityType::CommunityToString(community);
        oss << name << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

bool UpdateCommunity::IsEqual(const RoutingPolicyAction &community) const {
    const UpdateCommunity in_comm =
        static_cast<const UpdateCommunity&>(community);
    if (op_ == in_comm.op_)
        return (communities() == in_comm.communities());
    return false;
}

UpdateExtCommunity::UpdateExtCommunity(const std::vector<string> &communities,
                                       string op) {
    BOOST_FOREACH(const string &community, communities) {
        const ExtCommunity::ExtCommunityList list =
            ExtCommunity::ExtCommunityFromString(community);
        if (list.size())
            communities_.push_back(list[0]);
    }
    std::sort(communities_.begin(), communities_.end());
    ExtCommunity::ExtCommunityList::iterator it =
        std::unique(communities_.begin(), communities_.end());
    communities_.erase(it, communities_.end());

    if (strcmp(op.c_str(), "add") == 0) {
        op_ = ADD;
    } else if (strcmp(op.c_str(), "remove") == 0) {
        op_ = REMOVE;
    } else if (strcmp(op.c_str(), "set") == 0) {
        op_ = SET;
    }
}

void UpdateExtCommunity::operator()(BgpAttr *attr) const {
    if (!attr) return;
    const ExtCommunity *comm = attr->ext_community();
    BgpAttrDB *attr_db = attr->attr_db();
    BgpServer *server = attr_db->server();
    ExtCommunityDB *comm_db = server->extcomm_db();
    ExtCommunityPtr new_community = NULL;
    if (op_ == SET) {
        new_community = comm_db->SetAndLocate(comm, communities_);
    } else if (op_ == ADD) {
        new_community = comm_db->AppendAndLocate(comm, communities_);
    } else if (op_ == REMOVE) {
        new_community = comm_db->RemoveAndLocate(comm, communities_);
    }
    attr->set_ext_community(new_community);
}

string UpdateExtCommunity::ToString() const {
    ostringstream oss;
    if (op_ == SET) oss << "Extcommunity set [ ";
    else if  (op_ == ADD) oss << "Extcommunity add [ ";
    else if (op_ == REMOVE) oss << "Extcommunity remove [ ";

    BOOST_FOREACH(ExtCommunity::ExtCommunityValue community, communities()) {
        string name = ExtCommunity::ToString(community);
        oss << name << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

bool UpdateExtCommunity::IsEqual(const RoutingPolicyAction &community) const {
    const UpdateExtCommunity in_comm =
        static_cast<const UpdateExtCommunity&>(community);
    if (op_ == in_comm.op_)
        return (communities() == in_comm.communities());
    return false;
}

UpdateLocalPref::UpdateLocalPref(uint32_t local_pref)
    : local_pref_(local_pref) {
}

void UpdateLocalPref::operator()(BgpAttr *attr) const {
    attr->set_local_pref(local_pref_);
}

string UpdateLocalPref::ToString() const {
    ostringstream oss;
    oss << "local-pref " << local_pref_;
    return oss.str();
}

bool UpdateLocalPref::IsEqual(const RoutingPolicyAction &local_pref) const {
    const UpdateLocalPref in_lp =
        static_cast<const UpdateLocalPref&>(local_pref);
    return (local_pref_ == in_lp.local_pref_);
}

UpdateMed::UpdateMed(uint32_t med)
    : med_(med) {
}

void UpdateMed::operator()(BgpAttr *attr) const {
    attr->set_med(med_);
}

string UpdateMed::ToString() const {
    ostringstream oss;
    oss << "med " << med_;
    return oss.str();
}

bool UpdateMed::IsEqual(const RoutingPolicyAction &med) const {
    const UpdateMed in_med =
        static_cast<const UpdateMed&>(med);
    return (med_ == in_med.med_);
}
