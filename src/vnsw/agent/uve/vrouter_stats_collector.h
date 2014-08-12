/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_stats_collector_h
#define vnsw_agent_vrouter_stats_collector_h

#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <uve/agent_uve.h>

//Defines functionality to periodically export VRouter UVEs to collector.
//Runs in the context of "Agent::Uve" which has exclusion with "db::DBTable"
class VrouterStatsCollector : public StatsCollector {
public:
    VrouterStatsCollector(boost::asio::io_service &io, AgentUve *uve);
    virtual ~VrouterStatsCollector();

    bool Run();
    void Shutdown();
private:
    AgentUve *agent_uve_;
    DISALLOW_COPY_AND_ASSIGN(VrouterStatsCollector);
};

#endif //vnsw_agent_vrouter_stats_collector_h
