/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_init_h
#define vnsw_agent_uve_init_h

#include <uve/stats_collector.h>
#include <uve/agent_stats.h>

class AgentStatsCollector;
class VrouterStatsCollector;
class FlowStatsCollector;
class InterVnStatsCollector;
class AgentStatsSandeshContext;

class AgentUve {
public:
    static void Init(int time_interval = StatsCollector::stats_coll_time);
    static void Shutdown();
    static AgentStatsCollector *GetStatsCollector() {return agent_stats_collector_;};
    static VrouterStatsCollector *GetVrouterStatsCollector() {return vrouter_stats_collector_;};
    static FlowStatsCollector *GetFlowStatsCollector() {return flow_stats_collector_;};
    static InterVnStatsCollector *GetInterVnStatsCollector() {return inter_vn_stats_collector_;}
    static AgentStatsSandeshContext *GetIntfStatsSandeshContext() { return intf_stats_sandesh_ctx_; }
    static AgentStatsSandeshContext *GetVrfStatsSandeshContext() { return vrf_stats_sandesh_ctx_; }
    static AgentStatsSandeshContext *GetDropStatsSandeshContext() { return drop_stats_sandesh_ctx_; }
private:
    static int sock_;
    static AgentStatsCollector *agent_stats_collector_;
    static VrouterStatsCollector *vrouter_stats_collector_;
    static FlowStatsCollector *flow_stats_collector_;
    static InterVnStatsCollector *inter_vn_stats_collector_;
    static AgentStatsSandeshContext *intf_stats_sandesh_ctx_;
    static AgentStatsSandeshContext *vrf_stats_sandesh_ctx_;
    static AgentStatsSandeshContext *drop_stats_sandesh_ctx_;
    DISALLOW_COPY_AND_ASSIGN(AgentUve);
};

#endif //vnsw_agent_uve_init_h
