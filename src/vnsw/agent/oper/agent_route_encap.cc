/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/shared_ptr.hpp>

#include "oper/agent_route_encap.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

AgentRouteEncap::AgentRouteEncap(Agent *agent) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL) {
}

bool AgentRouteEncap::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    route->ResyncRoute();
    return true;
}

void AgentRouteEncap::Update() {
    StartVrfWalk(); 
}
