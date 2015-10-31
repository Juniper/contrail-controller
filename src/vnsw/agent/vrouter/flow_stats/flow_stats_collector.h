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
#include <vrouter/flow_stats/flow_export_request.h>

// Forward declaration
class AgentUtXmlFlowThreshold;
class AgentUtXmlFlowThresholdValidate;
class FlowStatsManager;

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

    typedef std::map<FlowKey, FlowEntryPtr, Inet4FlowKeyCmp> FlowEntryTree;

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
    void UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                               uint64_t pkts);
    void Shutdown();
    void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts);
    virtual void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    FlowEntryTree::const_iterator begin() const {
        return flow_tree_.begin();
    }

    FlowEntryTree::const_iterator end() const {
        return flow_tree_.end();
    }

    FlowEntryTree::const_iterator upper_bound(const FlowKey &flow_key) const {
        return flow_tree_.upper_bound(flow_key);
    }

    uint32_t size() const { return flow_tree_.size();}
    void set_deleted(bool val) {
        deleted_ = val;
    }

    uint32_t threshold() const;
    bool deleted() const {
        return deleted_;
    }

    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
    friend class FlowStatsManager;
private:
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
    bool RequestHandler(boost::shared_ptr<FlowExportReq> req);
    void AddFlow(const FlowKey &key, FlowEntryPtr ptr);
    void DeleteFlow(const FlowKey &key);
    void UpdateFlowIndex(const FlowKey &key, uint32_t idx);

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
    tbb::atomic<bool> deleted_;
    FlowAgingTableKey flow_aging_key_;
    FlowEntryTree flow_tree_;
    FlowStatsManager *flow_stats_manager_;
    WorkQueue<boost::shared_ptr<FlowExportReq> > request_queue_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
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
    static const uint8_t kCatchAllProto = 0xFF;
    static const uint64_t FlowThresoldUpdateTime = 1000 * 2;
    static const uint32_t kDefaultFlowSamplingThreshold = 500;

    typedef boost::shared_ptr<FlowStatsCollector> FlowAgingTablePtr;

    typedef std::map<const FlowAgingTableKey, FlowAgingTablePtr>
                     FlowAgingTableMap;
    typedef std::pair<const FlowAgingTableKey, FlowAgingTablePtr>
                     FlowAgingTableEntry;

    FlowStatsManager(Agent *agent);
    ~FlowStatsManager();

    Agent* agent() { return agent_; }
    FlowStatsCollector* default_flow_stats_collector() {
        return default_flow_stats_collector_.get();
    }

    //Add protocol + port based flow aging table
    void Add(const FlowAgingTableKey &key,
             uint64_t flow_stats_interval,
             uint64_t flow_cache_timeout);
    void Delete(const FlowAgingTableKey &key);
    void Free(const FlowAgingTableKey &key);

    //Add flow entry to particular aging table
    void AddEvent(FlowEntryPtr &flow);
    void DeleteEvent(FlowEntryPtr &flow);
    void FlowIndexUpdateEvent(const FlowKey &key, uint32_t idx);

    uint32_t flow_export_msg_drops() { return 0;}

    void Init(uint64_t flow_stats_interval, uint64_t flow_cache_timeout);
    void Shutdown();

    FlowAgingTableMap::iterator begin() {
        return flow_aging_table_map_.begin();
    }

    FlowAgingTableMap::iterator end() {
        return flow_aging_table_map_.end();
    }

    FlowStatsCollector* GetFlowStatsCollector(const FlowKey &key) const;
    const FlowStatsCollector* Find(uint32_t proto, uint32_t port) const;

    bool RequestHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    void AddReqHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    void DeleteReqHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    void FreeReqHandler(boost::shared_ptr<FlowStatsCollectorReq> req);
    uint32_t flow_export_count() const {
        return flow_export_count_;
    }

    void set_flow_export_count(uint32_t count) {
        flow_export_count_ = count;
    }

    uint32_t flow_export_count_reset() {
        return flow_export_count_.fetch_and_store(0);
    }

    uint32_t flow_export_msg_drops() const {
        return flow_export_msg_drops_;
    }

    void set_flow_export_msg_drops(uint32_t count) {
        flow_export_msg_drops_ = count;
    }
    uint32_t threshold() const { return threshold_;}
    bool delete_short_flow() const {
        return delete_short_flow_;
    }

    void set_delete_short_flow(bool val) {
        delete_short_flow_ = val;
    }

    static void FlowStatsReqHandler(Agent *agent, uint32_t proto,
                                    uint32_t port,
                                    uint64_t protocol);
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
private:
    friend class FlowStatsCollectorReq;
    friend class FlowStatsCollector;
    bool UpdateFlowThreshold(void);
    void UpdateThreshold(uint32_t new_value);
    Agent *agent_;
    WorkQueue<boost::shared_ptr<FlowStatsCollectorReq> > request_queue_;
    FlowAgingTableMap flow_aging_table_map_;
    FlowAgingTablePtr default_flow_stats_collector_;
    tbb::atomic<uint32_t> flow_export_count_;
    uint64_t prev_flow_export_rate_compute_time_;
    uint32_t flow_export_rate_;
    uint32_t threshold_;
    tbb::atomic<uint32_t> flow_export_msg_drops_;
    uint32_t prev_cfg_flow_export_rate_;
    Timer* timer_;
    bool delete_short_flow_;
};
#endif //vnsw_agent_flow_stats_collector_h
