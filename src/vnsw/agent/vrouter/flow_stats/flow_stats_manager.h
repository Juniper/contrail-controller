/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_maanger_h
#define vnsw_agent_flow_stats_maanger_h

#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <pkt/flow_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>

extern SandeshTraceBufferPtr FlowExportStatsTraceBuf;

#define FLOW_EXPORT_STATS_TRACE(...)\
do {\
    FlowExportStatsTrace::TraceMsg(FlowExportStatsTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);

class FlowStatsCollector;
class FlowStatsCollectorObject;

struct FlowAgingTableKey {
    FlowAgingTableKey(const uint8_t &protocol, const uint16_t &dst_port):
        proto(protocol), port(dst_port) {}

    bool operator==(const FlowAgingTableKey &rhs) const {
        return (proto == rhs.proto && port == rhs.port);
    }

    bool operator<(const FlowAgingTableKey &rhs) const {
        if (proto != rhs.proto) {
            return proto < rhs.proto;
        }

        return port < rhs.port;
    }

    uint8_t proto;
    uint16_t port;
};

struct FlowStatsCollectorReq {
    enum Event {
        INVALID,
        ADD_FLOW_STATS_COLLECTOR,
        DELETE_FLOW_STATS_COLLECTOR,
        FREE_FLOW_STATS_COLLECTOR,
    };

    FlowStatsCollectorReq(Event ev, const FlowAgingTableKey &k,
                          uint64_t interval, uint64_t timeout) :
        event(ev), key(k), flow_stats_interval(interval),
        flow_cache_timeout(timeout) {}

    FlowStatsCollectorReq(Event ev, const FlowAgingTableKey &k):
        event(ev), key(k) {}

    Event event;
    FlowAgingTableKey key;
    uint64_t flow_stats_interval;
    uint64_t flow_cache_timeout;
};

class FlowStatsManager {
public:
    static const uint8_t kCatchAllProto = 0x0;
    static const uint64_t FlowThresoldUpdateTime = 1000 * 2;
    static const uint32_t kDefaultFlowSamplingThreshold = 500;
    static const uint32_t kMinFlowSamplingThreshold = 20;

    typedef boost::shared_ptr<FlowStatsCollectorObject> FlowAgingTablePtr;

    typedef std::map<const FlowAgingTableKey, FlowAgingTablePtr>
                     FlowAgingTableMap;
    typedef std::pair<const FlowAgingTableKey, FlowAgingTablePtr>
                     FlowAgingTableEntry;

    FlowStatsManager(Agent *agent);
    ~FlowStatsManager();

    Agent* agent() { return agent_; }
    FlowStatsCollectorObject* default_flow_stats_collector_obj() {
        return default_flow_stats_collector_obj_.get();
    }

    //Add protocol + port based flow aging table
    void Add(const FlowAgingTableKey &key,
             uint64_t flow_stats_interval,
             uint64_t flow_cache_timeout);
    void Delete(const FlowAgingTableKey &key);
    void Free(const FlowAgingTableKey &key);

    //Add flow entry to particular aging table
    void AddEvent(FlowEntryPtr &flow);
    void DeleteEvent(const FlowEntryPtr &flow, const RevFlowDepParams &params);
    void UpdateStatsEvent(const FlowEntryPtr &flow, uint32_t bytes,
                          uint32_t packets, uint32_t oflow_bytes,
                          const boost::uuids::uuid &u);

    void Init(uint64_t flow_stats_interval, uint64_t flow_cache_timeout);
    void InitDone();
    void Shutdown();

    FlowAgingTableMap::iterator begin() {
        return flow_aging_table_map_.begin();
    }

    FlowAgingTableMap::iterator end() {
        return flow_aging_table_map_.end();
    }

    FlowStatsCollector* GetFlowStatsCollector(const FlowEntry *p) const;
    const FlowStatsCollectorObject* Find(uint32_t proto, uint32_t port) const;

    bool RequestHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    void AddReqHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    void DeleteReqHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    void FreeReqHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    uint32_t flow_export_rate() const {
        return flow_export_rate_;
    }
    uint32_t flow_export_count() const {
        return flow_export_count_;
    }

    void set_flow_export_count(uint32_t count) {
        flow_export_count_ = count;
    }

    uint32_t flow_export_count_reset() {
        return flow_export_count_.fetch_and_store(0);
    }

    uint32_t flow_export_without_sampling_reset() {
        return flow_export_without_sampling_.fetch_and_store(0);
    }

    uint32_t flow_export_disable_drops() const {
        return flow_export_disable_drops_;
    }

    uint32_t flow_export_sampling_drops() const {
        return flow_export_sampling_drops_;
    }

    uint32_t flow_export_drops() const {
        return flow_export_drops_;
    }

    uint32_t deleted_flow_export_drops() const {
        return deleted_flow_export_drops_;
    }

    uint64_t flow_sample_exports() const {
        return flow_sample_exports_;
    }

    uint64_t flow_msg_exports() const {
        return flow_msg_exports_;
    }

    uint64_t flow_exports() const {
        return flow_exports_;
    }

    uint64_t threshold() const { return threshold_;}
    bool delete_short_flow() const {
        return delete_short_flow_;
    }

    void set_delete_short_flow(bool val) {
        delete_short_flow_ = val;
    }
    void set_flows_sampled_atleast_once() {
        flows_sampled_atleast_once_ = true;
    }
    static void FlowStatsReqHandler(Agent *agent, uint32_t proto,
                                    uint32_t port,
                                    uint64_t protocol);
    void FreeIndex(uint32_t idx);
    uint32_t AllocateIndex();
    void UpdateFlowExportStats(uint32_t count, bool first_export,
                               bool sampled_flow);
    void UpdateFlowSampleExportStats(uint32_t count);
    void UpdateFlowMsgExportStats(uint32_t count);

    void SetProfileData(ProfileData *data);
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
private:
    friend class FlowStatsCollectorReq;
    friend class FlowStatsCollector;
    bool UpdateFlowThreshold(void);
    void UpdateThreshold(uint64_t new_value, bool check_oflow);
    FlowStatsCollectorObject* GetFlowStatsCollectorObject(const FlowEntry *flow)
        const;
    Agent *agent_;
    WorkQueue<boost::shared_ptr<FlowStatsCollectorReq> > request_queue_;
    FlowAgingTableMap flow_aging_table_map_;
    FlowAgingTablePtr default_flow_stats_collector_obj_;
    tbb::atomic<uint32_t> flow_export_count_;
    uint64_t prev_flow_export_rate_compute_time_;
    uint32_t flow_export_rate_;
    uint64_t threshold_;
    tbb::atomic<uint64_t> flow_export_disable_drops_;
    tbb::atomic<uint64_t> flow_export_sampling_drops_;
    tbb::atomic<uint32_t> flow_export_without_sampling_;
    tbb::atomic<uint64_t> flow_export_drops_;
    tbb::atomic<uint64_t> deleted_flow_export_drops_;
    tbb::atomic<uint64_t> flow_sample_exports_;
    tbb::atomic<uint64_t> flow_msg_exports_;
    tbb::atomic<uint64_t> flow_exports_;
    tbb::atomic<bool> flows_sampled_atleast_once_;
    uint32_t prev_cfg_flow_export_rate_;
    Timer* timer_;
    bool delete_short_flow_;
    //Protocol based array for minimal tree comparision
    FlowStatsCollectorObject* protocol_list_[256];
    IndexVector<FlowStatsCollector> instance_table_;
};
#endif //vnsw_agent_flow_stats_manager_h
