/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy_match.h"

#include <boost/foreach.hpp>

#include <algorithm>

#include <bgp/bgp_attr.h>
#include <net/community_type.h>

MatchCommunity::MatchCommunity(const std::vector<std::string> &communities) {
    BOOST_FOREACH(const std::string &community, communities) {
        uint32_t value = CommunityType::CommunityFromString(community);
        // Invalid community from config is ignored
        if (value) {
            to_match_.push_back(value);
        }
    }

    std::sort(to_match_.begin(), to_match_.end());
    std::vector<uint32_t>::iterator it =
        std::unique(to_match_.begin(), to_match_.end());
    to_match_.erase(it, to_match_.end());
}

MatchCommunity::~MatchCommunity() {
}

bool MatchCommunity::Match(const BgpRoute *route,
                                   const BgpAttr *attr) const {
    const Community *comm = attr->community();
    if (comm) {
        std::vector<uint32_t> list = comm->communities();
        if (list.size() < to_match_.size()) return false;
        std::sort(list.begin(), list.end());
        if (std::includes(list.begin(), list.end(),
                         to_match_.begin(), to_match_.end())) return true;
    }

    return false;
}

std::string MatchCommunity::ToString() const {
    return "Match community";
}

bool MatchCommunity::IsEqual(const RoutingPolicyMatch &community) const {
    const MatchCommunity in_comm =
        static_cast<const MatchCommunity&>(community);
    return (communities() == in_comm.communities());
}

template <typename T>
MatchPrefix<T>::MatchPrefix(const std::string &prefix,
                          const std::string &match_type) {
    boost::system::error_code ec;
    match_prefix_ = PrefixT::FromString(prefix, &ec);
    if (strcmp(match_type.c_str(), "exact") == 0) {
        match_type_ = EXACT;
    } else if (strcmp(match_type.c_str(), "longer") == 0) {
        match_type_ = LONGER;
    } else if (strcmp(match_type.c_str(), "orlonger") == 0) {
        match_type_ = ORLONGER;
    }
}

template <typename T>
MatchPrefix<T>::~MatchPrefix() {
}

template <typename T>
bool MatchPrefix<T>::Match(const BgpRoute *route,
                                const BgpAttr *attr) const {
    const RouteT *in_route = dynamic_cast<const RouteT *>(route);
    if (in_route == NULL) return false;
    const PrefixT &prefix = in_route->GetPrefix();
    if (match_type_ == EXACT) {
        if (prefix == match_prefix_) return true;
    } else if (match_type_ == LONGER) {
        if (prefix == match_prefix_) return false;
        if (prefix.IsMoreSpecific(match_prefix_)) return true;
    } else if (match_type_ == ORLONGER) {
        if (prefix.IsMoreSpecific(match_prefix_)) return true;
    }
    return false;
}

template <typename T>
bool MatchPrefix<T>::IsEqual(const RoutingPolicyMatch &prefix) const {
    const MatchPrefix in_prefix =
        static_cast<const MatchPrefix&>(prefix);
    if (match_type_ == in_prefix.match_type_)
        return (match_prefix_ == in_prefix.match_prefix_);
    return true;
}

template <typename T>
std::string MatchPrefix<T>::ToString() const {
    return "Match Prefix";
}

template class MatchPrefix<InetPrefixMatch>;
template class MatchPrefix<Inet6PrefixMatch>;
