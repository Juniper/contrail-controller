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

class ProfileData;

struct FlowStats {
    uint64_t add_count_;
    uint64_t delete_count_;
    uint64_t revaluate_count_;
    uint64_t audit_count_;

    FlowStats() :
        add_count_(0), delete_count_(0), revaluate_count_(0), audit_count_(0) {
    }
};

class FlowProto : public Proto {
public:
    typedef WorkQueue<FlowEvent> FlowEventQueue;
    static const int kMinTableCount = 1;
    static const int kMaxTableCount = 16;

    FlowProto(Agent *agent, boost::asio::io_service &io);
    virtual ~FlowProto();

    void Init();
    void InitDone();
    void Shutdown();
    void FlushFlows();

    bool Validate(PktInfo *msg);
    FlowHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                   boost::asio::io_service &io);
    bool Enqueue(boost::shared_ptr<PktInfo> msg);

    FlowEntry *Find(const FlowKey &key) const;
    uint16_t FlowTableIndex(uint16_t sport, uint16_t dport) const;
    FlowTable *GetTable(uint16_t index) const;
    FlowTable *GetFlowTable(const FlowKey &key) const;
    uint32_t FlowCount() const;
    void VnFlowCounters(const VnEntry *vn, uint32_t *in_count,
                        uint32_t *out_count);

    bool AddFlow(FlowEntry *flow);
    bool UpdateFlow(FlowEntry *flow);

    void EnqueueFlowEvent(const FlowEvent &event);
    void DeleteFlowRequest(const FlowKey &flow_key, bool del_rev_flow);
    void EvictFlowRequest(FlowEntry *flow, uint32_t flow_handle);
    void RetryIndexAcquireRequest(FlowEntry *flow, uint32_t flow_handle);
    void CreateAuditEntry(FlowEntry *flow);
    bool FlowEventHandler(const FlowEvent &req);
    void GrowFreeListRequest(const FlowKey &key);

    void DisableFlowEventQueue(uint32_t index, bool disabled);
    void DisableFlowMgmtQueue(bool disabled);
    const FlowStats *flow_stats() const { return &stats_; }

    void SetProfileData(ProfileData *data);
private:
    std::vector<FlowEventQueue *> flow_event_queue_;
    std::vector<FlowTable *> flow_table_list_;
    FlowEventQueue flow_update_queue_;
    FlowStats stats_;
};

extern SandeshTraceBufferPtr PktFlowTraceBuf;

#define PKTFLOW_TRACE(obj, ...)\
do {\
    PktFlow##obj::TraceMsg(PktFlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif // vnsw_agent_flow_proto_hpp
