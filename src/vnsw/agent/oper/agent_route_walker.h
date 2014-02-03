/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_walker_hpp
#define vnsw_agent_route_walker_hpp

#include "cmn/agent_cmn.h"

class AgentRouteWalker {
public:
    enum WalkType {
        UNICAST,
        MULTICAST,
        ALL,
    };

    typedef DBTableWalker::WalkId RouteWalkerIdList[Agent::ROUTE_TABLE_MAX];

    AgentRouteWalker(WalkType type);
    ~AgentRouteWalker() { };

    void StartVrfWalk();
    void CancelVrfWalk();

    //Route table walk for specified VRF
    void StartRouteWalk(const VrfEntry *vrf);
    void CancelRouteWalk();

    virtual bool VrfWalkNotify(DBTablePartBase *partition, 
                               DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, 
                                 DBEntryBase *e);

    virtual void VrfWalkDone(DBTableBase *part);
    virtual void RouteWalkDone(DBTableBase *part);

    virtual void RestartAgentRouteWalk();

private:
   AgentRouteWalker::WalkType walk_type_;    
   DBTableWalker::WalkId vrf_walkid_;
   DBTableWalker::WalkId route_walkid_[Agent::ROUTE_TABLE_MAX];
};

#endif
