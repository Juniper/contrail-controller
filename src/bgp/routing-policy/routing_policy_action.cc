/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy_action.h"

#include <boost/foreach.hpp>

#include <algorithm>

#include <bgp/bgp_attr.h>
#include <bgp/bgp_server.h>
#include <bgp/community.h>
#include <net/community_type.h>


UpdateCommunity::UpdateCommunity(const std::vector<std::string> communities,
                                 std::string op) {
    BOOST_FOREACH(const std::string &community, communities) {
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
    if (comm) {
        BgpAttrDB *attr_db = attr->attr_db();
        BgpServer *server = attr_db->server();
        CommunityDB *comm_db = server->comm_db();
        CommunityPtr new_community;
        if (op_ == SET) {
            new_community = comm_db->SetAndLocate(comm, communities_);
        } else if (op_ == ADD) {
            new_community = comm_db->AppendAndLocate(comm, communities_);
        } else if (op_ == REMOVE) {
            new_community = comm_db->RemoveAndLocate(comm, communities_);
        }
        attr->set_community(new_community);
    }
}

std::string UpdateCommunity::ToString() const {
    return "Update Community";
}

UpdateLocalPref::UpdateLocalPref(uint32_t local_pref)
    : local_pref_(local_pref) {
}

void UpdateLocalPref::operator()(BgpAttr *attr) const {
    attr->set_local_pref(local_pref_);
}

std::string UpdateLocalPref::ToString() const {
    return "Update Community";
}
