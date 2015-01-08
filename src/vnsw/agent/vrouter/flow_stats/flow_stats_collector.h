/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <uve/vm_uve_entry.h>
#include <pkt/flow_table.h>
#include <vrouter/ksync/flowtable_ksync.h>

//Defines the functionality to periodically read flow stats from
//shared memory (between agent and Kernel) and export this stats info to
//collector. Also responsible for aging of flow entries. Runs in the context
//of "Agent::StatsCollector" which has exclusion with "db::DBTable",
//"Agent::FlowHandler", "sandesh::RecvQueue", "bgp::Config" & "Agent::KSync"
class FlowStatsCollector : public StatsCollector {
public:
    static const uint64_t FlowAgeTime = 1000000 * 180;
    static const uint32_t FlowCountPerPass = 200;
    static const uint32_t FlowStatsMinInterval = (100); // time in milliseconds
    static const uint32_t MaxFlows= (256 * 1024); // time in milliseconds

    FlowStatsCollector(boost::asio::io_service &io, int intvl,
                       uint32_t flow_cache_timeout,
                       AgentUveBase *uve);
    virtual ~FlowStatsCollector();

    uint64_t flow_age_time_intvl() { return flow_age_time_intvl_; }
    uint32_t flow_age_time_intvl_in_secs() {
        return flow_age_time_intvl_/(1000 * 1000);
    }
    void UpdateFlowMultiplier();
    bool Run();
    void UpdateFlowAgeTime(uint64_t usecs) {
        flow_age_time_intvl_ = usecs;
        UpdateFlowMultiplier();
    }
    void UpdateFlowAgeTimeInSecs(uint32_t secs) {
        UpdateFlowAgeTime(secs * 1000 * 1000);
    }

    void UpdateFlowStats(FlowEntry *flow, uint64_t &diff_bytes,
                         uint64_t &diff_pkts);
    void UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                               uint64_t pkts);
    void Shutdown();
    void set_delete_short_flow(bool val) { delete_short_flow_ = val; }
private:
    void UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes, uint64_t pkts);
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowStats *stats, const vr_flow_entry *k_flow,
                      uint64_t curr_time);
    uint64_t GetUpdatedFlowPackets(const FlowStats *stats, uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowStats *stats, uint64_t k_flow_bytes);
    VmUveEntry::FloatingIp *ReverseFlowFip(const FlowEntry *flow);
    AgentUveBase *agent_uve_;
    FlowKey flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    uint32_t flow_count_per_pass_;
    uint32_t flow_multiplier_;
    uint32_t flow_default_interval_;
    // Should short-flow be deleted immediately?
    // Value will be set to false for test cases
    bool delete_short_flow_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

#endif //vnsw_agent_flow_stats_collector_h
