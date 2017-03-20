/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_resync_hpp
#define vnsw_agent_route_resync_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/agent_route_walker.h>

class AgentRouteResync : public AgentRouteWalker {
public:
    typedef DBTableWalker::WalkId RouteWalkerIdList[Agent::ROUTE_TABLE_MAX];
    AgentRouteResync(const std::string &name, Agent *agent);
    virtual ~AgentRouteResync();

    void Update();
    void UpdateRoutesInVrf(VrfEntry *vrf);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    DISALLOW_COPY_AND_ASSIGN(AgentRouteResync);
};

#endif
