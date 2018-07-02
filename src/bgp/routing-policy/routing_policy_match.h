/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_MATCH_H_
#define SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_MATCH_H_

#include <stdint.h>

#include <set>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "base/regex.h"
#include "bgp/bgp_config.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"

class BgpAttr;
class BgpPath;
class BgpRoute;
class Community;

class RoutingPolicyMatch {
public:
    virtual std::string ToString() const = 0;
    virtual ~RoutingPolicyMatch() {
    }
    bool operator()(const BgpRoute *route,
                    const BgpPath *path, const BgpAttr *attr) const {
        return Match(route, path, attr);
    }
    virtual bool Match(const BgpRoute *route,
                       const BgpPath *path, const BgpAttr *attr) const = 0;
    virtual bool operator==(const RoutingPolicyMatch &match) const {
        if (typeid(match) == typeid(*this))
            return(IsEqual(match));
        return false;
    }
    virtual bool operator!=(const RoutingPolicyMatch &match) const {
        return !operator==(match);
    }
    virtual bool IsEqual(const RoutingPolicyMatch &match) const = 0;
};

class MatchCommunity: public RoutingPolicyMatch {
public:
    typedef std::set<uint32_t> CommunityList;
    typedef std::vector<std::string> CommunityRegexStringList;
    typedef std::vector<contrail::regex> CommunityRegexList;

    MatchCommunity(const std::vector<std::string> &communities, bool match_all);
    virtual ~MatchCommunity();
    virtual bool Match(const BgpRoute *route,
                       const BgpPath *path, const BgpAttr *attr) const;
    virtual std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyMatch &community) const;
    bool match_all() const { return match_all_; }
    const CommunityList &communities() const { return to_match_; }
    const CommunityRegexStringList &regex_strings() const {
        return to_match_regex_strings_;
    }
    const CommunityRegexList &regexs() const { return to_match_regexs_; }

private:
    bool MatchAll(const BgpAttr *attr) const;
    bool MatchAny(const BgpAttr *attr) const;

    bool match_all_;
    CommunityList to_match_;
    CommunityRegexStringList to_match_regex_strings_;
    CommunityRegexList to_match_regexs_;
};

class MatchExtCommunity: public RoutingPolicyMatch {
public:
    typedef std::vector<std::string> CommunityRegexStringList;
    typedef std::vector<contrail::regex> CommunityRegexList;

    MatchExtCommunity(const std::vector<std::string> &communities,
                      bool match_all);
    virtual ~MatchExtCommunity();
    virtual bool Match(const BgpRoute *route,
                       const BgpPath *path, const BgpAttr *attr) const;
    virtual std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyMatch &community) const;
    bool match_all() const { return match_all_; }
    const ExtCommunity::ExtCommunityList &communities() const {
        return to_match_;
    }
    const CommunityRegexStringList &regex_strings() const {
        return to_match_regex_strings_;
    }
    const CommunityRegexList &regexs() const { return to_match_regexs_; }
    bool Find(const ExtCommunity::ExtCommunityValue &community) const;

private:
    bool MatchAll(const BgpAttr *attr) const;
    bool MatchAny(const BgpAttr *attr) const;
    const ExtCommunity::ExtCommunityList ExtCommunityFromString(
                        const std::string &comm);

    bool match_all_;
    ExtCommunity::ExtCommunityList to_match_;
    CommunityRegexStringList to_match_regex_strings_;
    CommunityRegexList to_match_regexs_;
};

class MatchProtocol: public RoutingPolicyMatch {
public:
    enum MatchProtocolType {
        Unspecified = 0,
        BGP,
        XMPP,
        StaticRoute,
        ServiceChainRoute,
        AggregateRoute,
        Interface,
        InterfaceStatic,
        ServiceInterface,
        BGPaaS
    };
    typedef std::vector<MatchProtocolType> PathSourceList;
    explicit MatchProtocol(const std::vector<std::string> &protocols);
    virtual ~MatchProtocol();
    virtual bool Match(const BgpRoute *route,
                       const BgpPath *path, const BgpAttr *attr) const;
    virtual std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyMatch &community) const;
    const PathSourceList &protocols() const {
        return to_match_;
    }

private:
    PathSourceList to_match_;
};
const std::string MatchProtocolToString(MatchProtocol::MatchProtocolType protocol);

template <typename T1, typename T2>
struct PrefixMatchBase {
    typedef T1 RouteT;
    typedef T2 PrefixT;
};

typedef PrefixMatchBase<InetRoute, Ip4Prefix> PrefixMatchInet;
typedef PrefixMatchBase<Inet6Route, Inet6Prefix> PrefixMatchInet6;

template <typename T>
class MatchPrefix : public RoutingPolicyMatch {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::PrefixT PrefixT;
    enum MatchType {
        EXACT,
        LONGER,
        ORLONGER,
    };

    class PrefixMatch {
    public:
        PrefixMatch(PrefixT prefix, MatchType match_type)
          : prefix(prefix), match_type(match_type) {
        }
        bool operator==(const PrefixMatch &rhs) const {
            return (prefix == rhs.prefix && match_type == rhs.match_type);
        }
        bool operator<(const PrefixMatch &rhs) const {
            BOOL_KEY_COMPARE(prefix, rhs.prefix);
            BOOL_KEY_COMPARE(match_type, rhs.match_type);
            return false;
        }

        PrefixT prefix;
        MatchType match_type;
    };

    explicit MatchPrefix(const PrefixMatchConfigList &match_config_list);
    virtual ~MatchPrefix();
    virtual bool Match(const BgpRoute *route,
                       const BgpPath *path, const BgpAttr *attr) const;
    virtual std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyMatch &prefix) const;

    static MatchType GetMatchType(const std::string &match_type_str);

private:
    template <typename U> friend class MatchPrefixTest;
    typedef std::vector<PrefixMatch> PrefixMatchList;

    PrefixMatchList match_list_;
};

typedef MatchPrefix<PrefixMatchInet> MatchPrefixInet;
typedef MatchPrefix<PrefixMatchInet6> MatchPrefixInet6;

#endif  // SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_MATCH_H_
