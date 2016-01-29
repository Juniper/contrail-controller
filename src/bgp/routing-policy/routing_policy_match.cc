/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy_match.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <sstream>

#include <bgp/bgp_attr.h>
#include <net/community_type.h>

using std::ostringstream;
using std::string;
using std::make_pair;

MatchCommunity::MatchCommunity(const std::vector<string> &communities) {
    BOOST_FOREACH(const string &community, communities) {
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

string MatchCommunity::ToString() const {
    ostringstream oss;
    oss << "community [ ";
    BOOST_FOREACH(uint32_t community, communities()) {
        string name = CommunityType::CommunityToString(community);
        oss << name << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

bool MatchCommunity::IsEqual(const RoutingPolicyMatch &community) const {
    const MatchCommunity in_comm =
        static_cast<const MatchCommunity&>(community);
    return (communities() == in_comm.communities());
}

template <typename T>
MatchPrefix<T>::MatchPrefix(const PrefixMatchConfigList &match_list) {
    BOOST_FOREACH(const PrefixMatchConfig &match, match_list) {
        boost::system::error_code ec;
        PrefixT match_prefix = PrefixT::FromString(match.prefix_to_match, &ec);
        MatchType match_type = EXACT;
        if (strcmp(match.prefix_match_type.c_str(), "exact") == 0) {
            match_type = EXACT;
        } else if (strcmp(match.prefix_match_type.c_str(), "longer") == 0) {
            match_type = LONGER;
        } else if (strcmp(match.prefix_match_type.c_str(), "orlonger") == 0) {
            match_type = ORLONGER;
        }
        match_list_.push_back(make_pair(match_prefix, match_type));
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
    BOOST_FOREACH(const PrefixMatch &match, match_list_) {
        if (match.second == EXACT) {
            if (prefix == match.first) return true;
        } else if (match.second == LONGER) {
            if (prefix == match.first) continue;
            if (prefix.IsMoreSpecific(match.first)) return true;
        } else if (match.second == ORLONGER) {
            if (prefix.IsMoreSpecific(match.first)) return true;
        }
    }
    return false;
}

template <typename T>
bool MatchPrefix<T>::IsEqual(const RoutingPolicyMatch &prefix) const {
    const MatchPrefix in_prefix =
        static_cast<const MatchPrefix&>(prefix);
    std::equal(in_prefix.match_list_.begin(), in_prefix.match_list_.end(),
               match_list_.begin());
    return true;
}

template <typename T>
string MatchPrefix<T>::ToString() const {
    ostringstream oss;
    oss << "prefix  [";
    BOOST_FOREACH(const PrefixMatch &match, match_list_) {
        oss << " " << match.first.ToString();
        if  (match.second == LONGER) oss << " longer";
        else if  (match.second == ORLONGER) oss << " orlonger";
        oss << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

template class MatchPrefix<InetPrefixMatch>;
template class MatchPrefix<Inet6PrefixMatch>;
