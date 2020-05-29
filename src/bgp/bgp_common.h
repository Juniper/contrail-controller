/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_COMMON_H_
#define SRC_BGP_BGP_COMMON_H_

#include <boost/intrusive_ptr.hpp>

#include <list>
#include <string>
#include <utility>
#include <vector>

#define BGP_RTGT_MIN_ID_AS2 8000000
#define BGP_RTGT_MIN_ID_AS4 8000

#define BGP_RTGT_MAX_ID_AS2 16777216
#define BGP_RTGT_MAX_ID_AS4 32768

typedef uint32_t as_t;
typedef uint16_t as2_t;
#define AS_TRANS 23456
#define AS2_MAX  0xFFFF

#define XMPP_HOLD_TIME_DEFAULT 90

class RoutingPolicy;
typedef boost::intrusive_ptr<RoutingPolicy> RoutingPolicyPtr;

//
// Generic data structure for configured list of routing policies with
// ordering info.
//
struct RoutingPolicyAttachInfo {
    std::string sequence_;
    std::string routing_policy_;
};

typedef std::vector<RoutingPolicyAttachInfo> RoutingPolicyConfigList;

//
// Generic data structure for policy attachment.
// It is a list of Routing Policy pointer + Generation of routing policy.
//
typedef std::pair<RoutingPolicyPtr, uint32_t> RoutingPolicyInfo;
typedef std::list<RoutingPolicyInfo> RoutingPolicyAttachList;

#endif  // SRC_BGP_BGP_COMMON_H_
