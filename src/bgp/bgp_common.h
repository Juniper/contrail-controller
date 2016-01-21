/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_common_h
#define ctrlplane_bgp_common_h
#include <list>
#include <vector>
#include <boost/intrusive_ptr.hpp>
typedef uint16_t as_t;
typedef uint32_t as4_t;

class RoutingPolicy;
typedef boost::intrusive_ptr<RoutingPolicy> RoutingPolicyPtr;

//
// Generic datastructure for configured list of routing policies with
// ordering info
//
struct RoutingPolicyAttachInfo {
    std::string sequence_;
    std::string routing_policy_;
};

typedef std::vector<RoutingPolicyAttachInfo> RoutingPolicyConfigList;

//
// Generic datastructure for policy attachment
// It is a list of Routing Policy pointer + Generation of routing policy
//
typedef std::pair<RoutingPolicyPtr, uint32_t> RoutingPolicyInfo;
typedef std::list<RoutingPolicyInfo> RoutingPolicyAttachList;

#endif
