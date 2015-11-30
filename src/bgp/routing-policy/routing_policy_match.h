/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_MATCH_H_
#define SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_MATCH_H_

#include <vector>
#include <string>

#include <stdint.h>

#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"

class BgpAttr;
class BgpRoute;
class Community;

class RoutingPolicyMatch {
public:
    virtual std::string ToString() const = 0;
    virtual ~RoutingPolicyMatch() {
    }
    bool operator()(const BgpRoute *route, const BgpAttr *attr) const {
        return Match(route, attr);
    }
    virtual bool Match(const BgpRoute *route, const BgpAttr *attr) const = 0;
};

class MatchCommunity: public RoutingPolicyMatch {
public:
    typedef std::vector<uint32_t> CommunityList;
    explicit MatchCommunity(const std::vector<std::string> &communities);
    virtual ~MatchCommunity();
    virtual bool Match(const BgpRoute *route, const BgpAttr *attr) const;
    virtual std::string ToString() const;
private:
    CommunityList to_match_;
};

template <typename T1, typename T2>
struct PrefixMatchBase {
  typedef T1 RouteT;
  typedef T2 PrefixT;
};

class InetPrefixMatch : public PrefixMatchBase<InetRoute, Ip4Prefix> {
};

class Inet6PrefixMatch : public PrefixMatchBase<Inet6Route, Inet6Prefix> {
};

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
    explicit MatchPrefix(const std::string &prefix,
                         const std::string &match_type="exact");
    virtual ~MatchPrefix();
    virtual bool Match(const BgpRoute *route, const BgpAttr *attr) const;
    virtual std::string ToString() const;
private:
    PrefixT match_prefix_;
    MatchType match_type_;
};

typedef MatchPrefix<InetPrefixMatch> PrefixMatchInet;
typedef MatchPrefix<Inet6PrefixMatch> PrefixMatchInet6;
#endif // SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_MATCH_H_
