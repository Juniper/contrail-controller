/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_h
#define vnsw_agent_flow_stats_h

#include <sandesh/common/flow_types.h>
#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <pkt/flowtable.h>
#include <ksync/flowtable_ksync.h>

struct FlowEntry;
struct PktInfo;
struct FlowKey;

class FlowStatsCollector : public StatsCollector {
public:
    static const uint64_t FlowAgeTime = 1000000 * 180;
    static const uint32_t FlowCountPerPass = 100;
    static const uint32_t FlowStatsInterval = (2000); // time in milliseconds

    FlowStatsCollector(boost::asio::io_service &io, int intvl) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       ("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector, 
                       io, intvl, "Flow stats collector") {
        flow_iteration_key_.Reset();
        flow_age_time_intvl_ = FlowAgeTime;
    }
    virtual ~FlowStatsCollector() { };

    static void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts);
    bool Run();
    uint64_t GetFlowAgeTime() { return flow_age_time_intvl_; }
    void SetFlowAgeTime(uint64_t usecs) { flow_age_time_intvl_ = usecs; }
private:
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowEntry *entry, const vr_flow_entry *k_flow,
                      uint64_t curr_time);
    static void SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow);
    FlowKey flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

#endif //vnsw_agent_flow_stats_h
