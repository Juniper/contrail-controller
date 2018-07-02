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
#include "bgp/extended-community/default_gateway.h"
#include "bgp/extended-community/es_import.h"
#include "bgp/extended-community/esi_label.h"
#include "bgp/extended-community/etree.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/extended-community/router_mac.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/extended-community/source_as.h"
#include "bgp/extended-community/tag.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "net/community_type.h"


using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
using std::includes;
using std::map;
using std::ostringstream;
using std::sort;
using std::string;
using std::unique;
using std::vector;
using std::find;

MatchCommunity::MatchCommunity(const vector<string> &communities,
    bool match_all) : match_all_(match_all) {
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
    if (match_all_) {
        oss << "community (all) [ ";
    } else {
        oss << "community (any) [ ";
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
    if (match_all() != in_community.match_all())
        return false;
    if (communities() != in_community.communities())
        return false;
    if (regex_strings() != in_community.regex_strings())
        return false;
    return true;
}

MatchExtCommunity::MatchExtCommunity(const vector<string> &communities,
    bool match_all) : match_all_(match_all) {
    // Assume that the each community string that doesn't correspond to a
    // community name or value is a regex string.
    BOOST_FOREACH(const string &community, communities) {
        const ExtCommunity::ExtCommunityList list =
            ExtCommunity::ExtCommunityFromString(community);
        if (list.size()) {
            if(!Find(list[0]))
                to_match_.push_back(list[0]);
        } else {
            to_match_regex_strings_.push_back(community);
        }
    }

    // Sort and uniquify the vector of regex strings.
    sort(to_match_.begin(), to_match_.end());
    sort(to_match_regex_strings_.begin(), to_match_regex_strings_.end());
    vector<string>::iterator it =
        unique(to_match_regex_strings_.begin(), to_match_regex_strings_.end());
    to_match_regex_strings_.erase(it, to_match_regex_strings_.end());

    // Build a vector of regexs corresponding to the regex strings.
    BOOST_FOREACH(string regex_str, regex_strings()) {
        to_match_regexs_.push_back(regex(regex_str));
    }
}

MatchExtCommunity::~MatchExtCommunity() {
}

//
// Return true if all community strings (normal or regex) are matched by the
// community values in the BgpAttr.
//
bool MatchExtCommunity::MatchAll(const BgpAttr *attr) const {
    // Bail if there's no community values in the BgpAttr.
    const ExtCommunity *comm = attr->ext_community();
    if (!comm)
        return false;

    // Make sure that all non-regex communities in this MatchExtCommunity are
    // present in the BgpAttr.
    ExtCommunity::ExtCommunityList list = comm->communities();
    sort(list.begin(), list.end());
    if (!includes(list.begin(), list.end(),
        communities().begin(), communities().end())) {
        return false;
    }

    // Make sure that each regex in this MatchExtCommunity is matched by one
    // of the communities in the BgpAttr.
    BOOST_FOREACH(const regex &match_expr, regexs()) {
        bool matched = false;
        BOOST_FOREACH(ExtCommunity::ExtCommunityValue community,
                      comm->communities()) {
            string community_str = ExtCommunity::ToString(community);
            if (regex_match(community_str, match_expr)) {
                matched = true;
                break;
            }
            community_str = ExtCommunity::ToHexString(community);
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

bool MatchExtCommunity::Find(const ExtCommunity::ExtCommunityValue &community)
                            const {
    return (std::find(communities().begin(), communities().end(), community) !=
            communities().end());
}

//
// Return true if any community strings (normal or regex) is matched by the
// community values in the BgpAttr.
//
bool MatchExtCommunity::MatchAny(const BgpAttr *attr) const {
    // Bail if there's no community values in the BgpAttr.
    const ExtCommunity *comm = attr->ext_community();
    if (!comm)
        return false;

    // Check if any of the community values in the BgpAttr matches one of
    // the normal community strings.
    BOOST_FOREACH(ExtCommunity::ExtCommunityValue community,
                  comm->communities()) {
        if (Find(community))
            return true;
    }

    // Check if any of the community values in the BgpAttr matches one of
    // the community regexs.
    BOOST_FOREACH(ExtCommunity::ExtCommunityValue community,
                  comm->communities()) {
        string community_str = ExtCommunity::ToString(community);
        BOOST_FOREACH(const regex &match_expr, regexs()) {
            if (regex_match(community_str, match_expr))
                return true;
        }
        community_str = ExtCommunity::ToHexString(community);
        BOOST_FOREACH(const regex &match_expr, regexs()) {
            if (regex_match(community_str, match_expr))
                return true;
        }
    }

    return false;
}

//
// Return true if the BgpPath matches this MatchExtCommunity.
//
bool MatchExtCommunity::Match(const BgpRoute *route, const BgpPath *path,
                              const BgpAttr *attr) const {
    return (match_all_ ? MatchAll(attr) : MatchAny(attr));
}

//
// Return string representation of this MatchExtCommunity.
//
string MatchExtCommunity::ToString() const {
    ostringstream oss;
    if (match_all_) {
        oss << "Extcommunity (all) [ ";
    } else {
        oss << "Extcommunity (any) [ ";
    }
    BOOST_FOREACH(ExtCommunity::ExtCommunityValue community, communities()) {
        string name = ExtCommunity::ToString(community);
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
// Return true if this MatchExtCommunity is equal to the one supplied.
//
bool MatchExtCommunity::IsEqual(const RoutingPolicyMatch &community) const {
    const MatchExtCommunity in_community =
        static_cast<const MatchExtCommunity &>(community);
    if (match_all() != in_community.match_all())
        return false;
    if (communities() != in_community.communities())
        return false;
    if (regex_strings() != in_community.regex_strings())
        return false;
    return true;
}

template <typename T>
typename MatchPrefix<T>::MatchType MatchPrefix<T>::GetMatchType(
    const string &match_type_str) {
    MatchType match_type = EXACT;
    if (match_type_str == "exact") {
        match_type = EXACT;
    } else if (match_type_str == "longer") {
        match_type = LONGER;
    } else if (match_type_str == "orlonger") {
        match_type = ORLONGER;
    }
    return match_type;
}

template <typename T>
MatchPrefix<T>::MatchPrefix(const PrefixMatchConfigList &match_config_list) {
    BOOST_FOREACH(const PrefixMatchConfig &match_config, match_config_list) {
        boost::system::error_code ec;
        PrefixT prefix = PrefixT::FromString(match_config.prefix_to_match, &ec);
        MatchType match_type =
            MatchPrefix<T>::GetMatchType(match_config.prefix_match_type);
        match_list_.push_back(PrefixMatch(prefix, match_type));
    }

    // Sort and uniquify the vector of PrefixMatch elements.
    sort(match_list_.begin(), match_list_.end());
    typename PrefixMatchList::iterator it =
        unique(match_list_.begin(), match_list_.end());
    match_list_.erase(it, match_list_.end());
}

template <typename T>
MatchPrefix<T>::~MatchPrefix() {
}

template <typename T>
bool MatchPrefix<T>::Match(const BgpRoute *route, const BgpPath *path,
                           const BgpAttr *attr) const {
    const RouteT *in_route = dynamic_cast<const RouteT *>(route);
    if (in_route == NULL)
        return false;
    const PrefixT &prefix = in_route->GetPrefix();
    BOOST_FOREACH(const PrefixMatch &prefix_match, match_list_) {
        if (prefix_match.match_type == EXACT) {
            if (prefix == prefix_match.prefix)
                return true;
        } else if (prefix_match.match_type == LONGER) {
            if (prefix == prefix_match.prefix)
                continue;
            if (prefix.IsMoreSpecific(prefix_match.prefix))
                return true;
        } else if (prefix_match.match_type == ORLONGER) {
            if (prefix.IsMoreSpecific(prefix_match.prefix))
                return true;
        }
    }
    return false;
}

template <typename T>
bool MatchPrefix<T>::IsEqual(const RoutingPolicyMatch &prefix) const {
    const MatchPrefix in_prefix = static_cast<const MatchPrefix&>(prefix);
    return (in_prefix.match_list_ == match_list_);
}

template <typename T>
string MatchPrefix<T>::ToString() const {
    ostringstream oss;
    oss << "prefix [";
    BOOST_FOREACH(const PrefixMatch &prefix_match, match_list_) {
        oss << " " << prefix_match.prefix.ToString();
        if  (prefix_match.match_type == LONGER) {
            oss << " longer";
        } else if  (prefix_match.match_type == ORLONGER) {
            oss << " orlonger";
        }
        oss << ",";
    }
    oss.seekp(-1, oss.cur);
    oss << " ]";
    return oss.str();
}

template class MatchPrefix<PrefixMatchInet>;
template class MatchPrefix<PrefixMatchInet6>;

static const map<string, MatchProtocol::MatchProtocolType> fromString
  = boost::assign::map_list_of
    ("bgp", MatchProtocol::BGP)
    ("xmpp", MatchProtocol::XMPP)
    ("static", MatchProtocol::StaticRoute)
    ("service-chain", MatchProtocol::ServiceChainRoute)
    ("aggregate", MatchProtocol::AggregateRoute)
    ("interface", MatchProtocol::Interface)
    ("interface-static", MatchProtocol::InterfaceStatic)
    ("service-interface", MatchProtocol::ServiceInterface)
    ("bgpaas", MatchProtocol::BGPaaS);

static const map<MatchProtocol::MatchProtocolType, string> toString
  = boost::assign::map_list_of
    (MatchProtocol::BGP, "bgp")
    (MatchProtocol::XMPP, "xmpp")
    (MatchProtocol::StaticRoute, "static")
    (MatchProtocol::ServiceChainRoute, "service-chain")
    (MatchProtocol::AggregateRoute, "aggregate")
    (MatchProtocol::Interface, "interface")
    (MatchProtocol::InterfaceStatic, "interface-static")
    (MatchProtocol::ServiceInterface, "service-interface")
    (MatchProtocol::BGPaaS, "bgpaas");

static const map<MatchProtocol::MatchProtocolType, BgpPath::PathSource>
    pathSourceMap = boost::assign::map_list_of
    (MatchProtocol::BGP, BgpPath::BGP_XMPP)
    (MatchProtocol::XMPP, BgpPath::BGP_XMPP)
    (MatchProtocol::StaticRoute, BgpPath::StaticRoute)
    (MatchProtocol::ServiceChainRoute, BgpPath::ServiceChain)
    (MatchProtocol::AggregateRoute, BgpPath::Aggregate)
    (MatchProtocol::Interface, BgpPath::BGP_XMPP)
    (MatchProtocol::InterfaceStatic, BgpPath::BGP_XMPP)
    (MatchProtocol::ServiceInterface, BgpPath::BGP_XMPP)
    (MatchProtocol::BGPaaS, BgpPath::BGP_XMPP);

static const vector<MatchProtocol::MatchProtocolType>
    isSubprotocol = boost::assign::list_of(MatchProtocol::Interface)
                                          (MatchProtocol::InterfaceStatic)
                                          (MatchProtocol::ServiceInterface)
                                          (MatchProtocol::BGPaaS)
                                          (MatchProtocol::StaticRoute);

static bool IsSubprotocol(MatchProtocol::MatchProtocolType protocol) {
    return (find(isSubprotocol.begin(), isSubprotocol.end(), protocol) !=
            isSubprotocol.end());
}

const string MatchProtocolToString(MatchProtocol::MatchProtocolType protocol) {
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
        // Ignore invalid protocol values.
        if (value == Unspecified)
            continue;
        to_match_.push_back(value);
    }

    // Sort and uniquify the vector match protocols.
    sort(to_match_.begin(), to_match_.end());
    vector<MatchProtocolType>::iterator it =
        unique(to_match_.begin(), to_match_.end());
    to_match_.erase(it, to_match_.end());
}

MatchProtocol::~MatchProtocol() {
}

bool MatchProtocol::Match(const BgpRoute *route, const BgpPath *path,
                           const BgpAttr *attr) const {
    BgpPath::PathSource path_src = path->GetSource();
    bool is_xmpp = path->GetPeer() ? path->GetPeer()->IsXmppPeer() : false;
    BOOST_FOREACH(MatchProtocolType protocol, protocols()) {
        if (IsSubprotocol(protocol)) {
            // Check only if matching protocol is subprotocol
            if (attr && !attr->sub_protocol().empty()) {
                std::string matchps = MatchProtocolToString(protocol);
                if (matchps.compare(attr->sub_protocol()) == 0)
                    return true;
            }
        } else {
            BgpPath::PathSource mapped_src =
                                PathSourceFromMatchProtocol(protocol);
            if (mapped_src != BgpPath::None) {
                if (mapped_src == path_src) {
                    if (protocol == XMPP && !is_xmpp)
                        continue;
                    if (protocol == BGP && is_xmpp)
                        continue;
                    return true;
                }
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
