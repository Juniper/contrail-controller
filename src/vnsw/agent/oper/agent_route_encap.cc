/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h> 
#include <agent_types.h>

#include <cmn/agent_db.h>

#include <oper/agent_route_walker.h>
#include <oper/agent_route_encap.h>
#include <oper/vrf.h>
#include <oper/agent_route.h>

AgentRouteEncap::AgentRouteEncap(Agent *agent) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL) {
}

bool AgentRouteEncap::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    route->EnqueueRouteResync();
    return true;
}

void AgentRouteEncap::Update() {
    StartVrfWalk(); 
}
