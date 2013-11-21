/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_init_h
#define vnsw_agent_uve_init_h

#include <uve/stats_collector.h>
#include <uve/agent_stats.h>
#include <uve/flow_stats.h>

class AgentStatsCollector;
class VrouterStatsCollector;
class FlowStatsCollector;
class InterVnStatsCollector;
class AgentStatsSandeshContext;

class AgentUve {
public:
    static const uint64_t band_intvl = (1000000); // time in microseconds
    AgentUve(Agent *agent);

    void Shutdown();
    AgentStatsCollector *GetStatsCollector() {return agent_stats_collector_;};
    VrouterStatsCollector *GetVrouterStatsCollector() {
        return vrouter_stats_collector_;
    };
    FlowStatsCollector *GetFlowStatsCollector() {
        return flow_stats_collector_;
    };
    InterVnStatsCollector *GetInterVnStatsCollector() {
        return inter_vn_stats_collector_;
    }
    AgentStatsSandeshContext *GetIntfStatsSandeshContext() {
        return intf_stats_sandesh_ctx_;
    }
    AgentStatsSandeshContext *GetVrfStatsSandeshContext() {
        return vrf_stats_sandesh_ctx_;
    }
    AgentStatsSandeshContext *GetDropStatsSandeshContext() {
        return drop_stats_sandesh_ctx_;
    }
    static AgentUve *GetInstance() {return singleton_;}

    void Init();
private:
    static AgentUve *singleton_;
    Agent *agent_;
    AgentStatsCollector *agent_stats_collector_;
    VrouterStatsCollector *vrouter_stats_collector_;
    FlowStatsCollector *flow_stats_collector_;
    InterVnStatsCollector *inter_vn_stats_collector_;
    AgentStatsSandeshContext *intf_stats_sandesh_ctx_;
    AgentStatsSandeshContext *vrf_stats_sandesh_ctx_;
    AgentStatsSandeshContext *drop_stats_sandesh_ctx_;
    DISALLOW_COPY_AND_ASSIGN(AgentUve);
};

#endif //vnsw_agent_uve_init_h
