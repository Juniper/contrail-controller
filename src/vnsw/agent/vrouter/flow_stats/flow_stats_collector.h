/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <pkt/flow_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>

// Forward declaration
class AgentUtXmlFlowThreshold;
class AgentUtXmlFlowThresholdValidate;

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
    static const uint32_t kDefaultFlowSamplingThreshold = 500;
    static const uint64_t FlowTcpSynAgeTime = 1000000 * 180;

    FlowStatsCollector(boost::asio::io_service &io, int intvl,
                       uint32_t flow_cache_timeout,
                       AgentUveBase *uve);
    virtual ~FlowStatsCollector();

    uint64_t flow_age_time_intvl() { return flow_age_time_intvl_; }
    uint32_t flow_age_time_intvl_in_secs() {
        return flow_age_time_intvl_/(1000 * 1000);
    }
    uint64_t flow_tcp_syn_age_time() const {
        return flow_tcp_syn_age_time_;
    }
    void set_flow_tcp_syn_age_time(uint64_t interval) {
        flow_tcp_syn_age_time_ = interval;
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
    void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts);
    virtual void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    void set_delete_short_flow(bool val) { delete_short_flow_ = val; }
    uint32_t flow_export_count()  const { return flow_export_count_; }
    void set_flow_export_count(uint32_t val) { flow_export_count_ = val; }
    uint64_t flow_export_msg_drops() const { return flow_export_msg_drops_; }
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
private:
    void UpdateFlowThreshold(uint64_t curr_time);
    void UpdateThreshold(uint32_t new_value);
    void UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes, uint64_t pkts);
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowStats *stats, const vr_flow_entry *k_flow,
                      uint64_t curr_time, const FlowEntry *flow);
    bool TcpFlowShouldBeAged(FlowStats *stats, const vr_flow_entry *k_flow,
                             uint64_t curr_time, const FlowEntry *flow);
    uint64_t GetUpdatedFlowPackets(const FlowStats *stats, uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowStats *stats, uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFip(const FlowEntry *flow);
    void SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow);
    void SetUnderlayInfo(FlowEntry *flow, FlowDataIpv4 &s_flow);
    bool SetUnderlayPort(FlowEntry *flow, FlowDataIpv4 &s_flow);
    AgentUveBase *agent_uve_;
    FlowKey flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    uint32_t flow_count_per_pass_;
    uint32_t flow_multiplier_;
    uint32_t flow_default_interval_;
    // Should short-flow be deleted immediately?
    // Value will be set to false for test cases
    bool delete_short_flow_;
    uint32_t flow_export_count_;
    uint64_t prev_flow_export_rate_compute_time_;
    uint32_t flow_export_rate_;
    uint32_t threshold_;
    uint64_t flow_export_msg_drops_;
    uint32_t prev_cfg_flow_export_rate_;
    uint64_t flow_tcp_syn_age_time_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

#endif //vnsw_agent_flow_stats_collector_h
