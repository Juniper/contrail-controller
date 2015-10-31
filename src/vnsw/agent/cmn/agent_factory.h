/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_factory_hpp
#define vnsw_agent_factory_hpp

#include <boost/function.hpp>
#include <base/factory.h>
#include <cmn/agent_cmn.h>

class Agent;
class AgentUveBase;
class KSync;
class DB;
class DBGraph;
class IFMapDependencyManager;
class InstanceManager;
class FlowStatsCollector;
class NexthopManager;
class FlowStatsManager;
class FlowAgingTableKey;

class AgentObjectFactory : public Factory<AgentObjectFactory> {
    FACTORY_TYPE_N1(AgentObjectFactory, KSync, Agent *);
    FACTORY_TYPE_N7(AgentObjectFactory, FlowStatsCollector,
                    boost::asio::io_service &, int, uint32_t, AgentUveBase *,
                    uint32_t , FlowAgingTableKey *,
                    FlowStatsManager *);
    FACTORY_TYPE_N4(AgentObjectFactory, AgentUveBase, Agent *, uint64_t,
                    uint32_t, uint32_t);
    FACTORY_TYPE_N1(AgentObjectFactory, AgentSignal, EventManager *);
    FACTORY_TYPE_N2(AgentObjectFactory, IFMapDependencyManager, DB *,
                    DBGraph *);
    FACTORY_TYPE_N1(AgentObjectFactory, InstanceManager,
                    Agent *);
    FACTORY_TYPE_N2(AgentObjectFactory, NexthopManager, EventManager *,
                    const std::string&);
};

#endif // vnsw_agent_factory_hpp

