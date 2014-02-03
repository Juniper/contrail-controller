/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_encap_hpp
#define vnsw_agent_route_encap_hpp

#include "cmn/agent_cmn.h"
#include "oper/agent_route_walker.h"

class AgentRouteEncap : public AgentRouteWalker {
public:    
    typedef DBTableWalker::WalkId RouteWalkerIdList[Agent::ROUTE_TABLE_MAX];
    AgentRouteEncap();
    ~AgentRouteEncap() { };

    void Update();

    virtual bool RouteWalkNotify(DBTablePartBase *partition,
                                 DBEntryBase *e);
};

#endif
