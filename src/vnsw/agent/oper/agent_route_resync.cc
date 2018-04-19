/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <cmn/agent_db.h>

#include <oper/agent_route_walker.h>
#include <oper/agent_route_resync.h>
#include <oper/vrf.h>
#include <oper/agent_route.h>
#include <oper/agent_path.h>

AgentRouteResync::AgentRouteResync(const std::string &name, Agent *agent) :
    AgentRouteWalker(name, agent) {
}

AgentRouteResync::~AgentRouteResync() {
}

bool AgentRouteResync::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    route->EnqueueRouteResync();
    return true;
}

void AgentRouteResync::Update() {
    StartVrfWalk();
}

void AgentRouteResync::UpdateRoutesInVrf(VrfEntry *vrf) {
    StartRouteWalk(vrf);
}
