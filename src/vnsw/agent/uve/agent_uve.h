/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_h
#define vnsw_agent_uve_h

#include <uve/agent_uve_base.h>
#include <uve/agent_stats_collector.h>
#include <uve/flow_stats_collector.h>

class VrouterStatsCollector;

//The class to drive UVE module initialization for agent
//Defines objects required for statistics collection from vrouter and
//objects required for sending UVE information to collector.
class AgentUve : public AgentUveBase {
public:
    AgentUve(Agent *agent, uint64_t intvl);
    virtual ~AgentUve();

    virtual void Shutdown();
    FlowStatsCollector *flow_stats_collector() const {
        return flow_stats_collector_.get();
    }
    // Update flow port bucket information
    void NewFlow(const FlowEntry *flow);
    void DeleteFlow(const FlowEntry *flow);
    virtual void RegisterDBClients();
    AgentStatsCollector *agent_stats_collector() const {
        return agent_stats_collector_.get();
    }
    VrouterStatsCollector *vrouter_stats_collector() const {
        return vrouter_stats_collector_.get();
    }

protected:
    boost::scoped_ptr<AgentStatsCollector> agent_stats_collector_;
    boost::scoped_ptr<FlowStatsCollector> flow_stats_collector_;
    boost::scoped_ptr<VrouterStatsCollector> vrouter_stats_collector_;

private:
    DISALLOW_COPY_AND_ASSIGN(AgentUve);
};

#endif //vnsw_agent_uve_h
