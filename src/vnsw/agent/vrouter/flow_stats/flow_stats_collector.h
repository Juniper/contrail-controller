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
//of kTaskFlowStatsCollector which has exclusion with "db::DBTable",
//
// The algorithm for ageing flows,
// - The complete flow-table will be scanned every 25% of ageing time
//   - An implication of this is, flow ageing will have accuracy of 25%
// - Run timer every kFlowStatsTimerInterval msec (100 msec)
// - Compute number of flow-entres to visit in kFlowStatsTimerInterval
//   - This is subject to constraing that complete flow table must be scanned
//     in 25% of ageing time
// - On every timer expiry accumulate the number of entries to visit into
//   entries_to_visit_ variable
// - Start a task (Flow AgeingTask) to scan the flow-entries
// - On every run of task, visit upto kFlowsPerTask entries
//   If scan is not complete, continue the task
//   On completion of scan, stop the task
//
// On every visit of flow, check if flow is idle for configured ageing time and
// delete the idle flows
class FlowStatsCollector : public StatsCollector {
public:
    // Default ageing time
    static const uint64_t FlowAgeTime = 1000000 * 180;
    // Default TCP ageing time
    static const uint64_t FlowTcpSynAgeTime = 1000000 * 180;

    // Time within which complete table must be scanned
    // Specified in terms of percentage of aging-time
    static const uint32_t kFlowScanTime = 25;
    // Flog ageing timer interval in milliseconds
    static const uint32_t kFlowStatsTimerInterval = 100;
    // Minimum flows to visit per interval
    static const uint32_t kMinFlowsPerTimer = 4000;
    // Number of flows to visit per task
    static const uint32_t kFlowsPerTask = 256;

    // Retry flow-delete after 5 second
    static const uint64_t kFlowDeleteRetryTime = (5 * 1000 * 1000);

    static const uint32_t kDefaultFlowSamplingThreshold = 500;
    static const uint8_t  kMaxFlowMsgsPerSend = 16;

    typedef std::map<const FlowEntry*, FlowExportInfo> FlowEntryTree;
    typedef WorkQueue<boost::shared_ptr<FlowExportReq> > Queue;

    // Task in which the actual flow table scan happens. See description above
    class AgeingTask : public Task {
    public:
        AgeingTask(FlowStatsCollector *fsc);
        virtual ~AgeingTask();
        bool Run();
        std::string Description() const;
    private:
        FlowStatsCollector *fsc_;
    };

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
    uint32_t threshold()  const;
    boost::uuids::uuid rand_gen();
    bool Run();
    bool RunAgeingTask();
    uint32_t RunAgeing(uint32_t max_count);
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
    void AddEvent(const FlowEntryPtr &flow);
    void DeleteEvent(const FlowEntryPtr &flow, const RevFlowDepParams &params);
    void SourceIpOverride(FlowExportInfo *info, FlowLogData &s_flow,
                          const RevFlowDepParams *params);
    void SetImplicitFlowDetails(FlowExportInfo *info, FlowLogData &s_flow,
                                const RevFlowDepParams *params);

    FlowExportInfo *FindFlowExportInfo(const FlowEntry *fe);
    const FlowExportInfo *FindFlowExportInfo(const FlowEntry *fe) const;
    void ExportFlow(FlowExportInfo *info, uint64_t diff_bytes,
                    uint64_t diff_pkts, const RevFlowDepParams *params);
    void UpdateFloatingIpStats(const FlowExportInfo *flow,
                               uint64_t bytes, uint64_t pkts);
    void UpdateStatsEvent(const FlowEntryPtr &flow, uint32_t bytes,
                          uint32_t packets, uint32_t oflow_bytes);
    size_t Size() const { return flow_tree_.size(); }
    void NewFlow(const FlowExportInfo &info);
    void set_deleted(bool val) {
        deleted_ = val;
    }
    bool deleted() const {
        return deleted_;
    }
    const FlowAgingTableKey& flow_aging_key() const {
        return flow_aging_key_;
    }
    int task_id() const { return task_id_; }
    uint32_t instance_id() const { return instance_id_; }
    const Queue *queue() const { return &request_queue_; }
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
    friend class FlowStatsRecordsReq;
    friend class FetchFlowStatsRecord;
    friend class FlowStatsManager;
protected:
    virtual void DispatchFlowMsg(const std::vector<FlowLogData> &lst);

private:
    static uint64_t GetCurrentTime();
    void ExportFlowLocked(FlowExportInfo *info, uint64_t diff_bytes,
                          uint64_t diff_pkts, const RevFlowDepParams *params);
    uint32_t TimersPerScan();
    void UpdateEntriesToVisit();
    void UpdateStatsAndExportFlow(FlowExportInfo *info, uint64_t teardown_time,
                                  const RevFlowDepParams *params);
    void EvictedFlowStatsUpdate(const FlowEntryPtr &flow,
                                uint32_t bytes,
                                uint32_t packets,
                                uint32_t oflow_bytes);
    void UpdateAndExportInternal(FlowExportInfo *info,
                                 uint32_t bytes,
                                 uint16_t oflow_bytes,
                                 uint32_t pkts,
                                 uint16_t oflow_pkts,
                                 uint64_t time,
                                 bool teardown_time,
                                 const RevFlowDepParams *params);
    void UpdateAndExportInternalLocked(FlowExportInfo *info,
                                       uint32_t bytes,
                                       uint16_t oflow_bytes,
                                       uint32_t pkts,
                                       uint16_t oflow_pkts,
                                       uint64_t time,
                                       bool teardown_time,
                                       const RevFlowDepParams *params);
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
    void FlowEvictEnqueue(FlowExportInfo *info, uint64_t t);
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
    uint64_t GetUpdatedFlowPackets(const FlowExportInfo *stats,
                                   uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowExportInfo *stats,
                                 uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFipEntry
        (const FlowExportInfo *flow);
    uint32_t ReverseFlowFip(const FlowExportInfo *info);
    VmInterfaceKey ReverseFlowFipVmi(const FlowExportInfo *info);
    bool RequestHandler(boost::shared_ptr<FlowExportReq> req);
    void AddFlow(FlowExportInfo info);
    void DeleteFlow(const FlowEntryPtr &flow);
    void HandleFlowStatsUpdate(const FlowKey &key, uint32_t bytes,
                               uint32_t packets, uint32_t oflow_bytes);

    void UpdateFlowStats(FlowExportInfo *flow, uint64_t &diff_bytes,
                         uint64_t &diff_pkts);
    uint8_t GetFlowMsgIdx();

    AgentUveBase *agent_uve_;
    int task_id_;
    boost::uuids::random_generator rand_gen_;
    const FlowEntry* flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    // Number of entries pending to be visited
    uint32_t entries_to_visit_;
    // Should short-flow be deleted immediately?
    // Value will be set to false for test cases
    bool delete_short_flow_;
    uint64_t flow_tcp_syn_age_time_;

    FlowEntryTree flow_tree_;
    Queue request_queue_;
    std::vector<FlowLogData> msg_list_;
    uint8_t msg_index_;
    tbb::atomic<bool> deleted_;
    FlowAgingTableKey flow_aging_key_;
    uint32_t instance_id_;
    FlowStatsManager *flow_stats_manager_;
    AgeingTask *ageing_task_;
    // Number of timer fires needed to scan the flow-table once
    // This is based on ageing timer
    uint32_t timers_per_scan_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};
#endif //vnsw_agent_flow_stats_collector_h
