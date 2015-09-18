/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <pkt/flow_table.h>
#include <pkt/flow_mgmt_request.h>
#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>
#include <vrouter/flow_stats/flow_export_request.h>
#include <vrouter/flow_stats/flow_export_info.h>

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
    static const uint64_t FlowTcpSynAgeTime = 1000000 * 180;
    static const uint32_t kDefaultFlowSamplingThreshold = 500;

    // Comparator for FlowEntryPtr
    struct FlowEntryCmp {
        bool operator()(const FlowKey &lhs, const FlowKey &rhs) {
            const FlowKey &lhs_base = static_cast<const FlowKey &>(lhs);
            return lhs_base.IsLess(rhs);
        }
    };

    typedef std::map<FlowKey, FlowExportInfo, FlowEntryCmp> FlowEntryTree;

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
    uint32_t flow_export_count()  const { return flow_export_count_; }
    void set_flow_export_count(uint32_t val) { flow_export_count_ = val; }
    uint64_t flow_export_msg_drops() const { return flow_export_msg_drops_; }
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
    void AddEvent(FlowEntryPtr &flow);
    void DeleteEvent(const FlowKey &key);
    void SourceIpOverride(const FlowKey &key, FlowExportInfo *info,
                          FlowDataIpv4 &s_flow);
    FlowExportInfo *FindFlowExportInfo(const FlowKey &flow);
    void ExportFlow(const FlowKey &key, FlowExportInfo *info,
                    uint64_t diff_bytes, uint64_t diff_pkts);
    void UpdateFloatingIpStats(const FlowExportInfo *flow,
                               uint64_t bytes, uint64_t pkts);
    void FlowIndexUpdateEvent(const FlowKey &key, uint32_t idx);
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
private:
    void FlowDeleteEnqueue(const FlowKey &key, bool rev);
    void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    void GetFlowSandeshActionParams(const FlowAction &action_info,
                                    std::string &action_str);
    void SetUnderlayInfo(FlowExportInfo *info, FlowDataIpv4 &s_flow);
    bool SetUnderlayPort(FlowExportInfo *info, FlowDataIpv4 &s_flow);
    void UpdateFlowThreshold(uint64_t curr_time);
    void UpdateThreshold(uint32_t new_value);

    void UpdateInterVnStats(FlowExportInfo *info,
                            uint64_t bytes, uint64_t pkts);
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowExportInfo *info, const vr_flow_entry *k_flow,
                      uint64_t curr_time, const FlowKey &key);
    bool TcpFlowShouldBeAged(FlowExportInfo *stats, const vr_flow_entry *k_flow,
                             uint64_t curr_time, const FlowKey &key);
    uint64_t GetUpdatedFlowPackets(const FlowExportInfo *stats,
                                   uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowExportInfo *stats,
                                 uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFipEntry
        (const FlowExportInfo *flow);
    uint32_t ReverseFlowFip(const FlowExportInfo *info);
    VmInterfaceKey ReverseFlowFipVmi(const FlowExportInfo *info);
    bool RequestHandler(boost::shared_ptr<FlowExportReq> req);
    void AddFlow(const FlowKey &key, FlowExportInfo info);
    void DeleteFlow(const FlowKey &key);
    void UpdateFlowIndex(const FlowKey &key, uint32_t idx);

    void UpdateFlowStats(FlowExportInfo *flow, uint64_t &diff_bytes,
                         uint64_t &diff_pkts);

    AgentUveBase *agent_uve_;
    FlowKey flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    uint32_t flow_count_per_pass_;
    uint32_t flow_multiplier_;
    uint32_t flow_default_interval_;
    // Should short-flow be deleted immediately?
    // Value will be set to false for test cases
    bool delete_short_flow_;
    uint64_t flow_tcp_syn_age_time_;
    FlowEntryTree flow_tree_;
    WorkQueue<boost::shared_ptr<FlowExportReq> > request_queue_;
    uint32_t flow_export_count_;
    uint64_t prev_flow_export_rate_compute_time_;
    uint32_t flow_export_rate_;
    uint32_t threshold_;
    uint64_t flow_export_msg_drops_;
    uint32_t prev_cfg_flow_export_rate_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

#endif //vnsw_agent_flow_stats_collector_h
