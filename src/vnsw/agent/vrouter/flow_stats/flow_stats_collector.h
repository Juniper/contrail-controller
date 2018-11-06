/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <boost/static_assert.hpp>
#include <pkt/flow_table.h>
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

struct KFlowData {
public:
    uint16_t underlay_src_port;
    uint16_t tcp_flags;
    uint16_t flags;
};

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
//
// The flow_tree_ maintains flows sorted on flow pointer. This tree cannot be
// used to scan flows for ageing since entries can be added/deleted between
// ageing tasks. Alternatively, another list is maintained in the sequence
// flows are added to flow ageing module.
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
    static const uint32_t kMinFlowsPerTimer = 3000;
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
                       FlowStatsManager *aging_module,
                       FlowStatsCollectorObject *obj);
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
    boost::uuids::uuid rand_gen();
    bool Run();
    bool RunAgeingTask();
    uint32_t ProcessFlow(FlowExportInfoList::iterator &it,
                         KSyncFlowMemory *ksync_obj,
                         FlowExportInfo *info, uint64_t curr_time);
    bool AgeFlow(KSyncFlowMemory *ksync_obj, const vr_flow_entry *k_flow,
                 const vr_flow_stats &k_stats, const KFlowData &kinfo,
                 FlowExportInfo *info, uint64_t curr_time);
    bool EvictFlow(KSyncFlowMemory *ksync_obj, const vr_flow_entry *k_flow,
                   uint16_t k_flow_flags, uint32_t flow_handle, uint16_t gen_id,
                   FlowExportInfo *info, uint64_t curr_time);
    uint32_t RunAgeing(uint32_t max_count);
    void UpdateFlowAgeTime(uint64_t usecs) {
        flow_age_time_intvl_ = usecs;
    }
    void UpdateFlowAgeTimeInSecs(uint32_t secs) {
        UpdateFlowAgeTime(secs * 1000 * 1000);
    }

    void UpdateFloatingIpStats(const FlowExportInfo *flow, uint64_t bytes,
                               uint64_t pkts);
    void UpdateStatsEvent(const FlowEntryPtr &flow, uint32_t bytes,
                          uint32_t packets, uint32_t oflow_bytes,
                          const boost::uuids::uuid &u);
    void Shutdown();
    void AddEvent(const FlowEntryPtr &flow);
    void DeleteEvent(const FlowEntryPtr &flow, const RevFlowDepParams &params);

    bool FindFlowExportInfo(const FlowEntry *fe, FlowEntryTree::iterator &it);
    FlowExportInfo *FindFlowExportInfo(const FlowEntry *fe);
    const FlowExportInfo *FindFlowExportInfo(const FlowEntry *fe) const;
    static uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    size_t Size() const { return flow_tree_.size(); }
    size_t AgeTreeSize() const { return flow_export_info_list_.size(); }
    void NewFlow(FlowEntry *flow);
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
    friend class FlowStatsCollectorObject;

private:
    static uint64_t GetCurrentTime();
    uint32_t TimersPerScan();
    void UpdateEntriesToVisit();
    void EvictedFlowStatsUpdate(const FlowEntryPtr &flow, uint32_t bytes,
                                uint32_t packets, uint32_t oflow_bytes,
                                const boost::uuids::uuid &u);
    void UpdateFlowStats(FlowExportInfo *info, uint64_t teardown_time);
    void UpdateFlowStatsInternalLocked(FlowExportInfo *info,
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
                                 bool teardown_time);
    void FlowDeleteEnqueue(FlowExportInfo *info, uint64_t t);
    void FlowEvictEnqueue(FlowExportInfo *info, uint64_t t,
                          uint32_t flow_handle, uint16_t gen_id);
    void UpdateThreshold(uint32_t new_value);

    void UpdateInterVnStats(FlowExportInfo *info,
                            uint64_t bytes, uint64_t pkts);
    void UpdateVmiTagBasedStats(FlowExportInfo *info,
                                uint64_t bytes, uint64_t pkts);
    bool ShouldBeAged(FlowExportInfo *info, const vr_flow_entry *k_flow,
                      const vr_flow_stats &k_stats, uint64_t curr_time);
    uint64_t GetUpdatedFlowPackets(const FlowExportInfo *stats,
                                   uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowExportInfo *stats,
                                 uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFipEntry
        (const FlowExportInfo *flow);
    uint32_t ReverseFlowFip(const FlowExportInfo *info);
    VmInterfaceKey ReverseFlowFipVmi(const FlowExportInfo *info);
    bool RequestHandler(boost::shared_ptr<FlowExportReq> req);
    bool RequestHandlerEntry();
    void RequestHandlerExit(bool done);
    void AddFlow(FlowExportInfo info);
    void DeleteFlow(FlowEntryTree::iterator &it);
    void UpdateFlowIterationKey(const FlowEntry *del_flow,
                                FlowEntryTree::iterator &tree_it);
    void HandleFlowStatsUpdate(const FlowKey &key, uint32_t bytes,
                               uint32_t packets, uint32_t oflow_bytes);

    AgentUveBase *agent_uve_;
    int task_id_;
    boost::uuids::random_generator rand_gen_;
    const FlowEntry* flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    // Number of entries pending to be visited
    uint32_t entries_to_visit_;
    uint64_t flow_tcp_syn_age_time_;

    FlowEntryTree flow_tree_;
    FlowExportInfoList flow_export_info_list_;
    // Flag to specify if flow-delete request event must be retried
    // If enabled
    //    Dont remove FlowExportInfo from list after generating delete event
    //    Retry delete event after kFlowDeleteRetryTime
    // Else
    //    Remove FlowExportInfo from list after generating delete event
    //    FIXME : disabling is only a debug feature for now. Once we remove
    //    from list, flow will never be aged. So, need to ensure all scenarios
    //    are covered before disabling the fag
    bool retry_delete_;
    Queue request_queue_;
    tbb::atomic<bool> deleted_;
    FlowAgingTableKey flow_aging_key_;
    uint32_t instance_id_;
    FlowStatsManager *flow_stats_manager_;
    FlowStatsCollectorObject *parent_;
    AgeingTask *ageing_task_;
    // Number of timer fires needed to scan the flow-table once
    // This is based on ageing timer
    uint32_t timers_per_scan_;
    // Cached UTC Time stamp
    // The timestamp is taken once on FlowStatsCollector::RequestHandlerEntry()
    // and used for all requests in current run
    uint64_t current_time_;
    uint64_t ageing_task_starts_;

    // Per ageing-timer stats for debugging
    uint32_t flows_visited_;
    uint32_t flows_aged_;
    uint32_t flows_evicted_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

class FlowStatsCollectorObject {
public:
    static const int kMaxCollectors = 2;
    typedef boost::shared_ptr<FlowStatsCollector> FlowStatsCollectorPtr;
    FlowStatsCollectorObject(Agent *agent, FlowStatsCollectorReq *req,
                             FlowStatsManager *mgr);
    FlowStatsCollector* GetCollector(uint8_t idx) const;
    void SetExpiryTime(int time);
    int GetExpiryTime() const;
    void MarkDelete();
    void ClearDelete();
    bool IsDeleted() const;
    void SetFlowAgeTime(uint64_t value);
    uint64_t GetFlowAgeTime() const;
    bool CanDelete() const;
    void Shutdown();
    FlowStatsCollector* FlowToCollector(const FlowEntry *flow);
    void UpdateAgeTimeInSeconds(uint32_t age_time);
    uint32_t GetAgeTimeInSeconds() const;
    size_t Size() const;
private:
    FlowStatsCollectorPtr collectors[kMaxCollectors];
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollectorObject);
};

#endif //vnsw_agent_flow_stats_collector_h
