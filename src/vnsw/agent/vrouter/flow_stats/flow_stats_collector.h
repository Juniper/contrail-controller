/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <pkt/flow_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>
#include <vrouter/flow_stats/flow_export_request.h>
#include <vrouter/flow_stats/flow_stats_manager.h>

// Forward declaration
class AgentUtXmlFlowThreshold;
class AgentUtXmlFlowThresholdValidate;
class FlowStatsManager;

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

    struct FlowKeyCmp {
        bool operator()(const FlowEntry *lhs, const FlowEntry *rhs) const {
            return lhs < rhs;
        }
    };

    typedef std::map<FlowEntry*, FlowEntryPtr, FlowKeyCmp> FlowEntryTree;

    FlowStatsCollector(boost::asio::io_service &io, int intvl,
                       uint32_t flow_cache_timeout,
                       AgentUveBase *uve, uint32_t instance_id,
                       FlowAgingTableKey *key,
                       FlowStatsManager *aging_module);
    virtual ~FlowStatsCollector();

    uint64_t flow_age_time_intvl() { return flow_age_time_intvl_; }
    void set_flow_age_time_intvl(uint64_t interval) {
        flow_age_time_intvl_ = interval;
    }

    uint32_t flow_age_time_intvl_in_secs() const {
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
    void UpdateAndExportFlowStats(FlowEntry *flow, uint64_t time,
                                  RevFlowDepParams *params);
    void UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                               uint64_t pkts);
    void Shutdown();
    void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts,
                    RevFlowDepParams *params);
    virtual void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    FlowEntryTree::const_iterator begin() const {
        return flow_tree_.begin();
    }

    FlowEntryTree::const_iterator end() const {
        return flow_tree_.end();
    }

    FlowEntryTree::const_iterator upper_bound(FlowEntry *ptr) const {
        return flow_tree_.upper_bound(ptr);
    }

    uint32_t size() const { return flow_tree_.size();}
    void set_deleted(bool val) {
        deleted_ = val;
    }

    uint32_t threshold() const;
    bool deleted() const {
        return deleted_;
    }

    FlowStatsManager *flow_stats_manager() const {
        return flow_stats_manager_;
    }

    const FlowAgingTableKey& flow_aging_key() const {
        return flow_aging_key_;
    }
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
    friend class FlowStatsManager;
private:
    void UpdateFlowStatsInternal(FlowEntry *flow, uint32_t bytes,
                                 uint16_t oflow_bytes, uint32_t pkts,
                                 uint16_t oflow_pkts, uint64_t time,
                                 bool teardown_time, uint64_t *diff_bytes,
                                 uint64_t *diff_pkts);
    void UpdateAndExportInternal(FlowEntry *flow, uint32_t bytes,
                                 uint16_t oflow_bytes, uint32_t pkts,
                                 uint16_t oflow_pkts, uint64_t time,
                                 bool teardown_time, RevFlowDepParams *params);
    uint64_t GetScanTime();
    void UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes, uint64_t pkts);
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowStats *stats, const vr_flow_entry *k_flow,
                      uint64_t curr_time, const FlowEntry *flow);
    bool TcpFlowShouldBeAged(FlowStats *stats, const vr_flow_entry *k_flow,
                             uint64_t curr_time, const FlowEntry *flow);
    uint64_t GetUpdatedFlowPackets(const FlowStats *stats, uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowStats *stats, uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFip(const FlowEntry *flow);
    void SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow,
                          RevFlowDepParams *params);
    void SetUnderlayInfo(FlowEntry *flow, FlowDataIpv4 &s_flow);
    bool SetUnderlayPort(FlowEntry *flow, FlowDataIpv4 &s_flow);
    bool RequestHandler(boost::shared_ptr<FlowExportReq> &req);
    void AddFlow(FlowEntryPtr ptr);
    void DeleteFlow(boost::shared_ptr<FlowExportReq> &req);
    void UpdateFlowStats(boost::shared_ptr<FlowExportReq> &req);

    AgentUveBase *agent_uve_;
    FlowEntry* flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    uint32_t flow_count_per_pass_;
    uint32_t flow_multiplier_;
    uint32_t flow_default_interval_;
    // Should short-flow be deleted immediately?
    // Value will be set to false for test cases
    bool delete_short_flow_;
    uint64_t flow_tcp_syn_age_time_;
    tbb::atomic<bool> deleted_;
    FlowAgingTableKey flow_aging_key_;
    FlowEntryTree flow_tree_;
    FlowStatsManager *flow_stats_manager_;
    WorkQueue<boost::shared_ptr<FlowExportReq> > request_queue_;
    uint32_t instance_id_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};
#endif //vnsw_agent_flow_stats_collector_h
