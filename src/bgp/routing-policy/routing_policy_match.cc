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

using boost::regex;
using boost::regex_match;
using std::includes;
using std::make_pair;
using std::map;
using std::ostringstream;
using std::sort;
using std::string;
using std::unique;
using std::vector;

MatchCommunity::MatchCommunity(const vector<string> &communities,
    bool singleton, bool match_all)
        : singleton_(singleton),
          match_all_(match_all) {
    // Assume that the each community string that doesn't correspond to a
    // community name or value is a regex string.
    BOOST_FOREACH(const string &community, communities) {
        uint32_t value = CommunityType::CommunityFromString(community);
        if (value) {
            to_match_.insert(value);
        } else {
            to_match_regex_strings_.push_back(community);
        }
    }

    // Sort and uniquify the vector of regex strings.
    sort(to_match_regex_strings_.begin(), to_match_regex_strings_.end());
    vector<string>::iterator it =
        unique(to_match_regex_strings_.begin(), to_match_regex_strings_.end());
    to_match_regex_strings_.erase(it, to_match_regex_strings_.end());

    // Build a vector of regexs corresponding to the regex strings.
    BOOST_FOREACH(string regex_str, regex_strings()) {
        to_match_regexs_.push_back(regex(regex_str));
    }
}

MatchCommunity::~MatchCommunity() {
}

//
// Return true if all community strings (normal or regex) are matched by the
// community values in the BgpAttr.
//
bool MatchCommunity::MatchAll(const BgpAttr *attr) const {
    // Bail if there's no community values in the BgpAttr.
    const Community *comm = attr->community();
    if (!comm)
        return false;

    // Make sure that all non-regex communities in this MatchCommunity are
    // present in the BgpAttr.
    vector<uint32_t> list = comm->communities();
    sort(list.begin(), list.end());
    if (!includes(list.begin(), list.end(),
        communities().begin(), communities().end())) {
        return false;
    }

    // Make sure that each regex in this MatchCommunity is matched by one
    // of the communities in the BgpAttr.
    BOOST_FOREACH(const regex &match_expr, regexs()) {
        bool matched = false;
        BOOST_FOREACH(uint32_t community, comm->communities()) {
            string community_str = CommunityType::CommunityToString(community);
            if (regex_match(community_str, match_expr)) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }

    return true;
}

//
// Return true if any community strings (normal or regex) is matched by the
// community values in the BgpAttr.
//
bool MatchCommunity::MatchAny(const BgpAttr *attr) const {
    // Bail if there's no community values in the BgpAttr.
    const Community *comm = attr->community();
    if (!comm)
        return false;

    // Check if any of the community values in the BgpAttr matches one of
    // the normal community strings.
    BOOST_FOREACH(uint32_t community, comm->communities()) {
        if (communities().find(community) != communities().end())
            return true;
    }

    // Check if any of the community values in the BgpAttr matches one of
    // the community regexs.
    BOOST_FOREACH(uint32_t community, comm->communities()) {
        string community_str = CommunityType::CommunityToString(community);
        BOOST_FOREACH(const regex &match_expr, regexs()) {
            if (regex_match(community_str, match_expr))
                return true;
        }
    }

    return false;
}

//
// Return true if the BgpPath matches this MatchCommunity.
//
bool MatchCommunity::Match(const BgpRoute *route, const BgpPath *path,
                           const BgpAttr *attr) const {
    return (match_all_ ? MatchAll(attr) : MatchAny(attr));
}

//
// Return string representation of this MatchCommunity.
//
string MatchCommunity::ToString() const {
    ostringstream oss;
    if (singleton_) {
        oss << "community [ ";
    } else if (match_all_) {
        oss << "community-all [ ";
    } else {
        oss << "community-any [ ";
    }
    BOOST_FOREACH(uint32_t community, communities()) {
        string name = CommunityType::CommunityToString(community);
        oss << name << ",";
    }
    BOOST_FOREACH(string regex_str, regex_strings()) {
        oss << regex_str << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

//
// Return true if this MatchCommunity is equal to the one supplied.
//
bool MatchCommunity::IsEqual(const RoutingPolicyMatch &community) const {
    const MatchCommunity in_community =
        static_cast<const MatchCommunity &>(community);
    if (singleton() != in_community.singleton())
        return false;
    if (match_all() != in_community.match_all())
        return false;
    if (communities() != in_community.communities())
        return false;
    if (regex_strings() != in_community.regex_strings())
        return false;
    return true;
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
