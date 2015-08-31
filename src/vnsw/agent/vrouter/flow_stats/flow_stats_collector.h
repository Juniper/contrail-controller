/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_h
#define vnsw_agent_flow_stats_collector_h

#include <pkt/flow_table.h>
#include <pkt/flow_mgmt_request.h>
#include <pkt/flow_mgmt_response.h>
#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>

// Forward declaration
class AgentUtXmlFlowThreshold;
class AgentUtXmlFlowThresholdValidate;

struct FlowExportInfo {
    FlowExportInfo(uint64_t time) : setup_time(time), teardown_time(0),
        last_modified_time(time), bytes(0), packets(0), underlay_source_port(0),
        underlay_sport_exported(false), exported(false) {}

    uint64_t setup_time;
    uint64_t teardown_time;
    uint64_t last_modified_time; //used for aging
    uint64_t bytes;
    uint64_t packets;
    //Underlay source port. 0 for local flows. Used during flow-export
    uint16_t underlay_source_port;
    bool underlay_sport_exported;
    bool exported;
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

    // Comparator for FlowEntryPtr
    struct FlowEntryRefCmp {
        bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
            FlowEntry *lhs = l.get();
            FlowEntry *rhs = r.get();

            return (lhs < rhs);
        }
    };

    // We want flow to be valid till Flow Management task is complete. So,
    // use FlowEntryPtr as key and hold reference to flow till we are done
    typedef std::map<FlowEntryPtr, FlowExportInfo, FlowEntryRefCmp>
        FlowEntryTree;
    FlowStatsCollector(boost::asio::io_service &io, int intvl,
                       uint32_t flow_cache_timeout,
                       AgentUveBase *uve);
    virtual ~FlowStatsCollector();

    uint64_t flow_age_time_intvl() { return flow_age_time_intvl_; }
    uint32_t flow_age_time_intvl_in_secs() {
        return flow_age_time_intvl_/(1000 * 1000);
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

    void UpdateFloatingIpStats(const FlowEntry *flow,
                               uint64_t bytes, uint64_t pkts);
    void Shutdown();
    void AddEvent(FlowEntry *low);
    void DeleteEvent(FlowEntry *flow);
    void ExportOnDelete(FlowEntry *flow);
    bool SourceIpOverride(FlowEntry *flow, uint32_t *ip) const;
    void set_delete_short_flow(bool val) { delete_short_flow_ = val; }
    FlowExportInfo *FindFlowExportInfo(const FlowEntryPtr &flow);
    void ExportFlowHandler(FlowEntry *flow, uint64_t diff_bytes,
                           uint64_t diff_pkts);
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
private:
    void ExportFlow(FlowEntry *flow, const FlowExportParams params);
    void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    void GetFlowSandeshActionParams(const FlowAction &action_info,
                                    std::string &action_str);
    void SetUnderlayInfo(FlowEntry *flow, FlowExportInfo *info,
                         FlowDataIpv4 &s_flow);
    bool SetUnderlayPort(FlowEntry *flow, FlowExportInfo *info,
                         FlowDataIpv4 &s_flow);
    void UpdateFlowThreshold(uint64_t curr_time);
    void UpdateThreshold(uint32_t new_value);

    void UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes, uint64_t pkts);
    uint64_t GetFlowStats(const uint16_t &oflow_data, const uint32_t &data);
    bool ShouldBeAged(FlowExportInfo *info, const vr_flow_entry *k_flow,
                      uint64_t curr_time);
    uint64_t GetUpdatedFlowPackets(const FlowExportInfo *stats,
                                   uint64_t k_flow_pkts);
    uint64_t GetUpdatedFlowBytes(const FlowExportInfo *stats,
                                 uint64_t k_flow_bytes);
    InterfaceUveTable::FloatingIp *ReverseFlowFipEntry(const FlowEntry *flow);
    uint32_t ReverseFlowFip(FlowEntry *fe) const;
    uint32_t ReverseFlowFipVmportId(FlowEntry *fe) const;
    bool RequestHandler(boost::shared_ptr<FlowMgmtRequest> req);
    bool ResponseHandler(const FlowMgmtResponse &resp);
    void ResponseEnqueue(const FlowMgmtResponse &resp) {
        response_queue_.Enqueue(resp);
    }
    void AddFlow(FlowEntryPtr &flow, uint64_t time);
    void DeleteFlow(FlowEntryPtr &flow);
    void LogFlow(FlowEntry *flow, const std::string &op);

    RevFlowParams BuildRevFlowParams(FlowEntry *fe) const;
    FlowExportParams BuildFlowExportParms(FlowEntry *fe);
    void UpdateFlowStats(FlowEntry *flow, uint64_t &diff_bytes,
                         uint64_t &diff_pkts);

    AgentUveBase *agent_uve_;
    FlowEntry *flow_iteration_key_;
    uint64_t flow_age_time_intvl_;
    uint32_t flow_count_per_pass_;
    uint32_t flow_multiplier_;
    uint32_t flow_default_interval_;
    // Should short-flow be deleted immediately?
    // Value will be set to false for test cases
    bool delete_short_flow_;
    FlowEntryTree flow_tree_;
    WorkQueue<boost::shared_ptr<FlowMgmtRequest> > request_queue_;
    WorkQueue<FlowMgmtResponse> response_queue_;
    uint32_t flow_export_count_;
    uint64_t prev_flow_export_rate_compute_time_;
    uint32_t flow_export_rate_;
    uint32_t threshold_;
    uint64_t flow_export_msg_drops_;
    uint32_t prev_cfg_flow_export_rate_;
    DISALLOW_COPY_AND_ASSIGN(FlowStatsCollector);
};

#endif //vnsw_agent_flow_stats_collector_h
