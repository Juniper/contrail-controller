/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_factory_hpp
#define vnsw_agent_factory_hpp

#include <boost/function.hpp>
#include <base/factory.h>
#include <cmn/agent_cmn.h>

class Agent;
class AgentUve;
class KSync;
class DB;
class DBGraph;
class IFMapDependencyManager;
class NamespaceManager;

class AgentObjectFactory : public Factory<AgentObjectFactory> {
    FACTORY_TYPE_N1(AgentObjectFactory, KSync, Agent *);
    FACTORY_TYPE_N2(AgentObjectFactory, AgentUve, Agent *, uint64_t);
    FACTORY_TYPE_N1(AgentObjectFactory, AgentSignal, EventManager *);
    FACTORY_TYPE_N2(AgentObjectFactory, IFMapDependencyManager, DB *,
                    DBGraph *);
    FACTORY_TYPE_N1(AgentObjectFactory, NamespaceManager,
                    EventManager *);
};

#endif // vnsw_agent_factory_hpp

