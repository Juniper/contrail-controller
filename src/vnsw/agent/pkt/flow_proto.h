/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_proto_hpp
#define vnsw_agent_flow_proto_hpp

#include <net/if.h>
#include "cmn/agent_cmn.h"
#include "base/queue_task.h"
#include "proto.h"
#include "proto_handler.h"
#include "flow_table.h"
#include "flow_handler.h"
#include "flow_event.h"
#include "flow_token.h"
#include "flow_trace_filter.h"

class ProfileData;

struct FlowStats {
    uint64_t add_count_;
    uint64_t delete_count_;
    uint64_t flow_messages_;
    uint64_t revaluate_count_;
    uint64_t recompute_count_;
    uint64_t audit_count_;
    uint64_t vrouter_responses_;
    uint64_t vrouter_error_;
    uint64_t evict_count_;

    // Number of events actually processed
    uint64_t delete_process_;
    uint64_t revaluate_process_;
    uint64_t recompute_process_;

    FlowStats() :
        add_count_(0), delete_count_(0), flow_messages_(0),
        revaluate_count_(0), recompute_count_(0), audit_count_(0),
        vrouter_responses_(0), vrouter_error_(0), evict_count_(0),
        delete_process_(0), revaluate_process_(0), recompute_process_(0) {
    }
};

class FlowProto : public Proto {
public:
    static const int kMinTableCount = 1;
    static const int kMaxTableCount = 16;

    FlowProto(Agent *agent, boost::asio::io_service &io);
    virtual ~FlowProto();

    void Init();
    void InitDone();
    void Shutdown();
    void FlushFlows();

    bool Validate(PktInfo *msg);
    FlowHandler *AllocProtoHandler(PktInfoPtr info,
                                   boost::asio::io_service &io);
    bool Enqueue(PktInfoPtr msg);

    FlowEntry *Find(const FlowKey &key, uint32_t table_index) const;
    uint16_t FlowTableIndex(const IpAddress &sip, const IpAddress &dip,
                            uint8_t proto, uint16_t sport,
                            uint16_t dport, uint32_t flow_handle) const;
    uint32_t flow_table_count() const { return flow_table_list_.size(); }
    FlowTable *GetTable(uint16_t index) const;
    FlowTable *GetFlowTable(const FlowKey &key, uint32_t flow_handle) const;
    uint32_t FlowCount() const;
    void VnFlowCounters(const VnEntry *vn, uint32_t *in_count,
                        uint32_t *out_count);
    void InterfaceFlowCount(const Interface *intf, uint64_t *created,
                            uint64_t *aged, uint32_t *active_flows) const;

    bool AddFlow(FlowEntry *flow);
    bool UpdateFlow(FlowEntry *flow);

    void EnqueueFlowEvent(FlowEvent *event);
    void ForceEnqueueFreeFlowReference(FlowEntryPtr &flow);
    void DeleteFlowRequest(FlowEntry *flow);
    void EvictFlowRequest(FlowEntry *flow, uint32_t flow_handle,
                          uint8_t gen_id, uint8_t evict_gen_id);
    void CreateAuditEntry(const FlowKey &key, uint32_t flow_handle,
                          uint8_t gen_id);
    bool FlowEventHandler(FlowEvent *req, FlowTable *table);
    bool FlowUpdateHandler(FlowEvent *req);
    bool FlowDeleteHandler(FlowEvent *req, FlowTable *table);
    bool FlowKSyncMsgHandler(FlowEvent *req, FlowTable *table);
    void GrowFreeListRequest(FlowTable *table);
    void KSyncEventRequest(KSyncEntry *ksync_entry,
                           KSyncEntry::KSyncEvent event, uint32_t flow_handle,
                           uint8_t gen_id, int ksync_error,
                           uint64_t evict_flow_bytes,
                           uint64_t evict_flow_packets,
                           int32_t evict_flow_oflow);
    void MessageRequest(FlowEntry *flow);

    void DisableFlowEventQueue(uint32_t index, bool disabled);
    void DisableFlowUpdateQueue(bool disabled);
    void DisableFlowKSyncQueue(uint32_t index, bool disabled);
    void DisableFlowDeleteQueue(uint32_t index, bool disabled);
    size_t FlowUpdateQueueLength();

    const FlowStats *flow_stats() const { return &stats_; }

    void SetProfileData(ProfileData *data);
    uint32_t linklocal_flow_count() const { return linklocal_flow_count_; }
    void update_linklocal_flow_count(int val) {
        int tmp = linklocal_flow_count_.fetch_and_add(val);
        if (val < 0)
            assert(tmp >= val);
    }
    bool EnqueueReentrant(boost::shared_ptr<PktInfo> msg,
                          uint8_t table_index);
    bool ShouldTrace(const FlowEntry *flow, const FlowEntry *rflow);
    void EnqueueUnResolvedFlowEntry(FlowEntry *flow);

    virtual void TokenAvailable(TokenPool *pool_base);
    TokenPtr GetToken(FlowEvent::Event event);
    bool TokenCheck(const FlowTokenPool *pool) const;


private:
    friend class SandeshIPv4FlowFilterRequest;
    friend class SandeshIPv6FlowFilterRequest;
    friend class SandeshShowFlowFilterRequest;
    friend class FlowTraceFilterTest;
    friend class FlowUpdateTest;
    friend class FlowTest;
    FlowTraceFilter *ipv4_trace_filter() { return &ipv4_trace_filter_; }
    FlowTraceFilter *ipv6_trace_filter() { return &ipv6_trace_filter_; }

    bool ProcessFlowEvent(const FlowEvent &req, FlowTable *table);
    bool FlowStatsUpdate() const;

    FlowTokenPool add_tokens_;
    FlowTokenPool ksync_tokens_;
    FlowTokenPool del_tokens_;
    FlowTokenPool update_tokens_;
    std::vector<FlowEventQueue *> flow_event_queue_;
    std::vector<FlowEventQueue *> flow_tokenless_queue_;
    std::vector<DeleteFlowEventQueue *> flow_delete_queue_;
    std::vector<KSyncFlowEventQueue *> flow_ksync_queue_;
    std::vector<FlowTable *> flow_table_list_;
    UpdateFlowEventQueue flow_update_queue_;
    tbb::atomic<int> linklocal_flow_count_;
    bool use_vrouter_hash_;
    FlowTraceFilter ipv4_trace_filter_;
    FlowTraceFilter ipv6_trace_filter_;
    FlowStats stats_;
    Timer *stats_update_timer_;
};

extern SandeshTraceBufferPtr PktFlowTraceBuf;

#define PKTFLOW_TRACE(obj, ...)\
do {\
    PktFlow##obj::TraceMsg(PktFlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif // vnsw_agent_flow_proto_hpp
