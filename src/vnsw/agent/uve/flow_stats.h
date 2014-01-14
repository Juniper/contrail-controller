/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_h
#define vnsw_agent_flow_stats_h

#include <sandesh/common/flow_types.h>
#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <vr_flow.h>
#include <pkt/flow_table.h>
#include <ksync/flowtable_ksync.h>

struct FlowEntry;
struct PktInfo;
struct FlowKey;

class FlowStatsCollector : public StatsCollector {
public:
    static const uint64_t FlowAgeTime = 1000000 * 180;
    static const uint32_t FlowCountPerPass = 200;
    static const uint32_t FlowStatsInterval = (1000); // time in milliseconds
    static const uint32_t FlowStatsMinInterval = (100); // time in milliseconds
    static const uint32_t MaxFlows= (256 * 1024); // time in milliseconds

    FlowStatsCollector(boost::asio::io_service &io, int intvl) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       ("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector, 
                       io, intvl, "Flow stats collector") {
        flow_iteration_key_.Reset();
        flow_default_interval_ = intvl;
        flow_age_time_intvl_ = FlowAgeTime;
        flow_count_per_pass_ = FlowCountPerPass;
        UpdateFlowMultiplier();
    }
    virtual ~FlowStatsCollector() { };
    void UpdateFlowMultiplier() {
        uint32_t age_time_millisec = flow_age_time_intvl_ / 1000;
        if (age_time_millisec == 0) {
            age_time_millisec = 1;
        }
        uint64_t default_age_time_millisec = FlowAgeTime / 1000;
        uint64_t max_flows = (MaxFlows * age_time_millisec) / default_age_time_millisec;
        flow_multiplier_ = (max_flows * FlowStatsMinInterval)/age_time_millisec;
    }

    static void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts);
    bool Run();
    uint64_t GetFlowAgeTime() { return flow_age_time_intvl_; }
    void SetFlowAgeTime(uint64_t usecs) { 
        flow_age_time_intvl_ = usecs; 
        UpdateFlowMultiplier();
    }
    void UpdateFlowStats(FlowEntry *flow, uint64_t &diff_bytes, 
                         uint64_t &diff_pkts);
private:
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowStats *stats, const vr_flow_entry *k_flow,
                      uint64_t curr_time);
    static void SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow);
    uint64_t GetUpdatedFlowPackets(const FlowStats *stats, uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowStats *stats, uint64_t k_flow_bytes);
    FlowKey flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    uint32_t flow_count_per_pass_;
    uint32_t flow_multiplier_;
    uint32_t flow_default_interval_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

#endif //vnsw_agent_flow_stats_h
