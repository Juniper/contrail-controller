/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_listener.h"

#include <boost/assign/list_of.hpp>

#include "ifmap/ifmap_dependency_tracker.h"

using namespace boost::assign;
using namespace std;

BgpConfigListener::BgpConfigListener(BgpConfigManager *manager)
    : IFMapConfigListener(manager, "bgp::Config") {
}

//
// Populate the policy with entries for each interesting identifier type.
//
// Note that identifier types and metadata types will have the same name
// when there's a link with attributes.  This happens because we represent
// such links with a "middle node" which stores all the attributes and add
// plain links between the original nodes and the middle node. An example
// is "bgp-peering".
//
// Additional unit tests should be added to bgp_config_listener_test.cc as
// and when this policy is modified.
//
void BgpConfigListener::DependencyTrackerInit() {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    IFMapDependencyTracker::NodeEventPolicy *policy =
      get_dependency_tracker()->policy_map();

    ReactionMap bgp_peering_react = map_list_of<string, PropagateList>
        ("bgp-peering", list_of("self"));
    policy->insert(make_pair("bgp-peering", bgp_peering_react));

    ReactionMap bgp_router_react = map_list_of<string, PropagateList>
        ("self", list_of("bgp-peering"));
    policy->insert(make_pair("bgp-router", bgp_router_react));

    ReactionMap rt_instance_react = map_list_of<string, PropagateList>
        ("instance-target", list_of("self")("connection"))
        ("connection", list_of("self"))
        ("virtual-network-routing-instance", list_of("self"));
    policy->insert(make_pair("routing-instance", rt_instance_react));

    ReactionMap virtual_network_react = map_list_of<string, PropagateList>
        ("self", list_of("virtual-network-routing-instance"));
    policy->insert(make_pair("virtual-network", virtual_network_react));
}
