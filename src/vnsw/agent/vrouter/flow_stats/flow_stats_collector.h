/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <pkt/flow_table.h>
#include <pkt/flow_mgmt_request.h>
#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>
#include <vrouter/flow_stats/flow_export_request.h>
#include <vrouter/flow_stats/flow_export_info.h>
#include <vrouter/flow_stats/flow_stats_manager.h>

// Forward declaration
class AgentUtXmlFlowThreshold;
class AgentUtXmlFlowThresholdValidate;
class FlowStatsRecordsReq;
class FetchFlowStatsRecord;
class FlowStatsManager;

//Defines the functionality to periodically read flow stats from
//shared memory (between agent and Kernel) and export this stats info to
//collector. Also responsible for aging of flow entries. Runs in the context
//of "Agent::StatsCollector" which has exclusion with "db::DBTable",
class FlowStatsCollector : public StatsCollector {
public:
    static const uint64_t FlowAgeTime = 1000000 * 180;
    static const uint32_t kFlowStatsTimerInterval = 50; // time in milliseconds
    static const uint64_t FlowTcpSynAgeTime = 1000000 * 180;
    // Retry flow-delete after 2 second
    static const uint64_t kFlowDeleteRetryTime = (2 * 1000 * 1000);

    // Time within which complete table must be scanned
    // Specified in terms of percentage of aging-time
    static const uint8_t  kFlowScanTime = 25;
    // Flow timer interval
    static const uint32_t kFlowStatsInterval = 50;
    // Minimum flows to visit per interval
    static const uint32_t kMinFlowsPerTimer = 500;

    static const uint32_t kDefaultFlowSamplingThreshold = 500;
    static const uint8_t  kMaxFlowMsgsPerSend = 16;

    typedef std::map<boost::uuids::uuid, FlowExportInfo> FlowEntryTree;

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
    uint32_t flow_export_count()  const { return flow_export_count_; }
    void set_flow_export_count(uint32_t val) { flow_export_count_ = val; }
    uint32_t flow_export_rate()  const { return flow_export_rate_; }
    uint32_t threshold()  const;
    uint64_t flow_export_msg_drops() const { return flow_export_msg_drops_; }
    boost::uuids::uuid rand_gen();
    bool Run();
    void UpdateFlowAgeTime(uint64_t usecs) {
        flow_age_time_intvl_ = usecs;
    }
    void UpdateFlowAgeTimeInSecs(uint32_t secs) {
        UpdateFlowAgeTime(secs * 1000 * 1000);
    }

    void UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                               uint64_t pkts);
    void Shutdown();
    void set_delete_short_flow(bool val) { delete_short_flow_ = val; }
    void AddEvent(FlowEntryPtr &flow);
    void DeleteEvent(const boost::uuids::uuid &u);
    void SourceIpOverride(FlowExportInfo *info, FlowLogData &s_flow);
    FlowExportInfo *FindFlowExportInfo(const boost::uuids::uuid &u);
    const FlowExportInfo *FindFlowExportInfo(const boost::uuids::uuid &u) const;
    void ExportFlow(FlowExportInfo *info, uint64_t diff_bytes,
                    uint64_t diff_pkts);
    void UpdateFloatingIpStats(const FlowExportInfo *flow,
                               uint64_t bytes, uint64_t pkts);
    void FlowIndexUpdateEvent(const boost::uuids::uuid &u, uint32_t idx);
    void UpdateStatsEvent(const boost::uuids::uuid &u, uint32_t bytes,
                          uint32_t packets, uint32_t oflow_bytes);
    size_t Size() const { return flow_tree_.size(); }
    void NewFlow(const FlowExportInfo &info);
    void set_deleted(bool val) {
        deleted_ = val;
    }
    bool deleted() const {
        return deleted_;
    }
    bool user_configured() const { return user_configured_; }
    void set_user_configured(bool value) { user_configured_ = value; }
    const FlowAgingTableKey& flow_aging_key() const {
        return flow_aging_key_;
    }
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
    friend class FlowStatsRecordsReq;
    friend class FetchFlowStatsRecord;
    friend class FlowStatsManager;
protected:
    virtual void DispatchFlowMsg(const std::vector<FlowLogData> &lst);

private:
    uint64_t GetScanTime();
    void UpdateAgingParameters();
    void UpdateStatsAndExportFlow(FlowExportInfo *info, uint64_t teardown_time);
    void EvictedFlowStatsUpdate(const boost::uuids::uuid &u,
                                uint32_t bytes,
                                uint32_t packets,
                                uint32_t oflow_bytes);
    void UpdateAndExportInternal(FlowExportInfo *info,
                                 uint32_t bytes,
                                 uint16_t oflow_bytes,
                                 uint32_t pkts,
                                 uint16_t oflow_pkts,
                                 uint64_t time,
                                 bool teardown_time);
    void UpdateFlowStatsInternal(FlowExportInfo *info,
                                 uint32_t bytes,
                                 uint16_t oflow_bytes,
                                 uint32_t pkts,
                                 uint16_t oflow_pkts,
                                 uint64_t time,
                                 bool teardown_time,
                                 uint64_t *diff_bytes,
                                 uint64_t *diff_pkts);
    void FlowDeleteEnqueue(FlowExportInfo *info, uint64_t t);
    void EnqueueFlowMsg();
    void DispatchPendingFlowMsg();
    void GetFlowSandeshActionParams(const FlowAction &action_info,
                                    std::string &action_str);
    void SetUnderlayInfo(FlowExportInfo *info, FlowLogData &s_flow);
    void UpdateThreshold(uint32_t new_value);

    void UpdateInterVnStats(FlowExportInfo *info,
                            uint64_t bytes, uint64_t pkts);
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowExportInfo *info, const vr_flow_entry *k_flow,
                      uint64_t curr_time);
    bool TcpFlowShouldBeAged(FlowExportInfo *stats, const vr_flow_entry *k_flow,
                             uint64_t curr_time);
    uint64_t GetUpdatedFlowPackets(const FlowExportInfo *stats,
                                   uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowExportInfo *stats,
                                 uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFipEntry
        (const FlowExportInfo *flow);
    uint32_t ReverseFlowFip(const FlowExportInfo *info);
    VmInterfaceKey ReverseFlowFipVmi(const FlowExportInfo *info);
    bool RequestHandler(boost::shared_ptr<FlowExportReq> req);
    void AddFlow(const boost::uuids::uuid &key, FlowExportInfo info);
    void DeleteFlow(const boost::uuids::uuid &key);
    void UpdateFlowIndex(const boost::uuids::uuid &u, uint32_t idx);
    void HandleFlowStatsUpdate(const FlowKey &key, uint32_t bytes,
                               uint32_t packets, uint32_t oflow_bytes);

    void UpdateFlowStats(FlowExportInfo *flow, uint64_t &diff_bytes,
                         uint64_t &diff_pkts);
    uint8_t GetFlowMsgIdx();

    AgentUveBase *agent_uve_;
    boost::uuids::random_generator rand_gen_;
    boost::uuids::uuid flow_iteration_key_;
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
    uint64_t flow_export_msg_drops_;
    uint32_t prev_cfg_flow_export_rate_;
    std::vector<FlowLogData> msg_list_;
    uint8_t msg_index_;
    tbb::atomic<bool> deleted_;
    FlowAgingTableKey flow_aging_key_;
    uint32_t instance_id_;
    FlowStatsManager *flow_stats_manager_;
    bool user_configured_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};
#endif //vnsw_agent_flow_stats_collector_h
