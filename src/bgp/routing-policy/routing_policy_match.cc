/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy_match.h"

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include <algorithm>
#include <map>
#include <sstream>
#include <vector>

#include "bgp/ipeer.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_path.h"
#include "net/community_type.h"

using std::ostringstream;
using std::string;
using std::make_pair;
using std::vector;
using std::map;

MatchCommunity::MatchCommunity(const vector<string> &communities) {
    BOOST_FOREACH(const string &community, communities) {
        uint32_t value = CommunityType::CommunityFromString(community);
        // Invalid community from config is ignored
        if (value) {
            to_match_.push_back(value);
        }
    }

    std::sort(to_match_.begin(), to_match_.end());
    vector<uint32_t>::iterator it =
        std::unique(to_match_.begin(), to_match_.end());
    to_match_.erase(it, to_match_.end());
}

MatchCommunity::~MatchCommunity() {
}

bool MatchCommunity::Match(const BgpRoute *route, const BgpPath *path,
                           const BgpAttr *attr) const {
    const Community *comm = attr->community();
    if (comm) {
        vector<uint32_t> list = comm->communities();
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
bool MatchPrefix<T>::Match(const BgpRoute *route, const BgpPath *path,
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
    return (in_prefix.match_list_ == match_list_);
    //std::equal(in_prefix.match_list_.begin(), in_prefix.match_list_.end(), match_list_.begin());
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

static const map<string, MatchProtocol::MatchProtocolType> fromString
  = boost::assign::map_list_of
    ("bgp", MatchProtocol::BGP)
    ("xmpp", MatchProtocol::XMPP)
    ("static", MatchProtocol::StaticRoute)
    ("service-chain", MatchProtocol::ServiceChainRoute)
    ("aggregate", MatchProtocol::AggregateRoute);

static const map<MatchProtocol::MatchProtocolType, string> toString
  = boost::assign::map_list_of
    (MatchProtocol::BGP, "bgp")
    (MatchProtocol::XMPP, "xmpp")
    (MatchProtocol::StaticRoute, "static")
    (MatchProtocol::ServiceChainRoute, "service-chain")
    (MatchProtocol::AggregateRoute, "aggregate");

static const map<MatchProtocol::MatchProtocolType, BgpPath::PathSource>
    pathSourceMap = boost::assign::map_list_of
    (MatchProtocol::BGP, BgpPath::BGP_XMPP)
    (MatchProtocol::XMPP, BgpPath::BGP_XMPP)
    (MatchProtocol::StaticRoute, BgpPath::StaticRoute)
    (MatchProtocol::ServiceChainRoute, BgpPath::ServiceChain)
    (MatchProtocol::AggregateRoute, BgpPath::Aggregate);

static const string MatchProtocolToString(
                              MatchProtocol::MatchProtocolType protocol) {
    map<MatchProtocol::MatchProtocolType, string>::const_iterator it =
        toString.find(protocol);
    if (it != toString.end()) {
        return it->second;
    }
    return "unspecified";
}

static MatchProtocol::MatchProtocolType MatchProtocolFromString(
                                                const string &protocol) {
    map<string, MatchProtocol::MatchProtocolType>::const_iterator it =
        fromString.find(protocol);
    if (it != fromString.end()) {
        return it->second;
    }
    return MatchProtocol::Unspecified;
}

static BgpPath::PathSource PathSourceFromMatchProtocol(
                                   MatchProtocol::MatchProtocolType src) {
    map<MatchProtocol::MatchProtocolType, BgpPath::PathSource>::const_iterator
        it = pathSourceMap.find(src);
    if (it != pathSourceMap.end()) {
        return it->second;
    }
    return BgpPath::None;
}

MatchProtocol::MatchProtocol(const vector<string> &protocols) {
    BOOST_FOREACH(const string &protocol, protocols) {
        MatchProtocolType value = MatchProtocolFromString(protocol);
        // Invalid community from config is ignored
        if (value != Unspecified) {
            to_match_.push_back(value);
        }
    }
}

MatchProtocol::~MatchProtocol() {
}

bool MatchProtocol::Match(const BgpRoute *route, const BgpPath *path,
                           const BgpAttr *attr) const {
    BgpPath::PathSource path_src = path->GetSource();
    bool is_xmpp = path->GetPeer() ? path->GetPeer()->IsXmppPeer() : false;
    BOOST_FOREACH(MatchProtocolType protocol, protocols()) {
        BgpPath::PathSource mapped_src = PathSourceFromMatchProtocol(protocol);
        if (mapped_src != BgpPath::None) {
            if (mapped_src == path_src) {
                if (protocol == XMPP && !is_xmpp) continue;
                if (protocol == BGP && is_xmpp) continue;
                return true;
            }
        }
    }
    return false;
}

string MatchProtocol::ToString() const {
    ostringstream oss;
    oss << "protocol [ ";
    BOOST_FOREACH(MatchProtocolType protocol, protocols()) {
        string name = MatchProtocolToString(protocol);
        oss << name << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

bool MatchProtocol::IsEqual(const RoutingPolicyMatch &protocol) const {
    const MatchProtocol in_protocol =
        static_cast<const MatchProtocol&>(protocol);
    return (protocols() == in_protocol.protocols());
}
