/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_stats_h
#define vnsw_agent_uve_stats_h

#include <uve/agent_uve_base.h>
#include <uve/stats_manager.h>

//The class to drive UVE module initialization for agent
//Defines objects required for statistics collection from vrouter and
//objects required for sending UVE information to collector.
class AgentUveStats : public AgentUveBase {
public:
    AgentUveStats(Agent *agent, uint64_t intvl, uint32_t default_intvl,
                  uint32_t incremental_intvl);
    virtual ~AgentUveStats();

    virtual void Shutdown();
    virtual void RegisterDBClients();
    StatsManager *stats_manager() const;

protected:
    boost::scoped_ptr<StatsManager> stats_manager_;

private:
    DISALLOW_COPY_AND_ASSIGN(AgentUveStats);
};

#endif //vnsw_agent_uve_stats_h
