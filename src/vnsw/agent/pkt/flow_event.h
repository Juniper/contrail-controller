/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EVENT_H__
#define __AGENT_FLOW_EVENT_H__

#include <sys/resource.h>
#include <ksync/ksync_entry.h>
#include "flow_table.h"

class FlowTokenPool;

////////////////////////////////////////////////////////////////////////////
// Control events for flow management
////////////////////////////////////////////////////////////////////////////
class FlowEvent {
public:
    enum Event {
        INVALID,
        // Flow add message from VRouter
        VROUTER_FLOW_MSG,
        // Message to update a flow
        FLOW_MESSAGE,
        // Event to delete a flow entry
        DELETE_FLOW,
        // Event by audit module to delete a flow
        AUDIT_FLOW,
        // In agent, flow is evicted if index is allocated for another flow
        // We delete the flow on eviction. There is a corner case where evicted
        // flow is added in parallel with different index. In that case
        // we ignore the operation
        EVICT_FLOW,
        // Revaluate flow due to deletion of a DBEntry. Other than for INET
        // routes, delete of a DBEntry will result in deletion of flows using
        // the DBEntry
        DELETE_DBENTRY,
        // Revaluate route due to change in a DBEntry. This event is used to
        // revaluate a flow on add/change of interface, vm, vn etc...
        // The action typically invovles only re-evaluating ACL lookup actions
        REVALUATE_DBENTRY,
        // Add/Delete of route can result in flow using a next higher/lower
        // prefix. The event will recompute the complete flow due to change
        // in route used for flow
        RECOMPUTE_FLOW,
        // Flow entry should be freed from kTaskFlowEvent task context.
        // Event to ensure flow entry is freed from right context
        FREE_FLOW_REF,
        // A DBEntry should be freed from kTaskFlowEvent task context.
        // Event to ensure DBEntry entry reference is freed from right context
        FREE_DBENTRY,
        // Grow the free-list entries for flow and ksync
        GROW_FREE_LIST,
        // Generate KSync event for the flow
        KSYNC_EVENT,
        // Pkt is re-entering processing in new partition
        REENTRANT,
        // Need to resolve the Flow entry whic is depending on Mirror entry
        UNRESOLVED_FLOW_ENTRY,
    };

    FlowEvent() :
        event_(INVALID), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), evict_gen_id_(0),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), evict_gen_id_(0), table_index_(0) {
    }

    FlowEvent(Event event, FlowEntry *flow) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        table_index_(0) {
    }

    FlowEvent(Event event, uint32_t table_index) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), evict_gen_id_(0),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(table_index) {
    }

    FlowEvent(Event event, FlowEntry *flow, uint32_t flow_handle,
              uint8_t gen_id) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        gen_id_(gen_id), evict_gen_id_(0), flow_handle_(flow_handle),
        table_index_(0) {
    }

    FlowEvent(Event event, FlowEntry *flow, uint32_t flow_handle,
              uint8_t gen_id, uint8_t evict_gen_id) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        gen_id_(gen_id), evict_gen_id_(evict_gen_id), flow_handle_(flow_handle),
        table_index_(0) {
    }

    FlowEvent(Event event, FlowEntry *flow, const DBEntry *db_entry) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(db_entry),
        gen_id_(0), evict_gen_id_(0),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, const DBEntry *db_entry, uint32_t gen_id) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(db_entry),
        gen_id_(gen_id), evict_gen_id_(0),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, uint16_t table_index, const DBEntry *db_entry,
              uint32_t gen_id) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(db_entry),
        gen_id_(gen_id), evict_gen_id_(0),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(table_index) {
    }

    FlowEvent(Event event, const FlowKey &key) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), evict_gen_id_(0), flow_key_(key),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, const FlowKey &key, uint32_t flow_handle,
              uint8_t gen_id) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(gen_id), evict_gen_id_(0), flow_key_(key),
        flow_handle_(flow_handle), table_index_(0) {
    }

    FlowEvent(Event event, PktInfoPtr pkt_info, FlowEntry *flow,
              uint32_t table_index) :
        event_(event), flow_(flow), pkt_info_(pkt_info), db_entry_(NULL),
        gen_id_(0), evict_gen_id_(0), flow_key_(),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(table_index) {
    }

    FlowEvent(const FlowEvent &rhs) :
        event_(rhs.event_), flow_(rhs.flow()), pkt_info_(rhs.pkt_info_),
        db_entry_(rhs.db_entry_), gen_id_(rhs.gen_id_),
        evict_gen_id_(rhs.evict_gen_id_), flow_key_(rhs.flow_key_),
        flow_handle_(rhs.flow_handle_), table_index_(rhs.table_index_) {
    }

    virtual ~FlowEvent() {
    }

    Event event() const { return event_; }
    FlowEntry *flow() const { return flow_.get(); }
    FlowEntryPtr &flow_ref() { return flow_; }
    void set_flow(FlowEntry *flow) { flow_ = flow; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }
    uint32_t gen_id() const { return gen_id_; }
    uint32_t evict_gen_id() const { return evict_gen_id_; }
    const FlowKey &get_flow_key() const { return flow_key_; }
    PktInfoPtr pkt_info() const { return pkt_info_; }
    uint32_t flow_handle() const { return flow_handle_; }
    uint32_t table_index() const { return table_index_;}
private:
    Event event_;
    FlowEntryPtr flow_;
    PktInfoPtr pkt_info_;
    const DBEntry *db_entry_;
    uint32_t gen_id_;
    uint32_t evict_gen_id_;
    FlowKey flow_key_;
    uint32_t flow_handle_;
    uint32_t table_index_;
};

////////////////////////////////////////////////////////////////////////////
// Event to process VRouter response for flow operation. VRouter response for
// flow is made of two messages,
// - vr_flow response which will contains,
//   - Return code for the operation
//   - flow-handle allocated for flow
//   - gen-id for he hash-entry allocated
//   - stats for the flow being evicted by VRouter
// - vr_response
//   - contains ksync-event to be generated for the flow
//
// The event combines data from both the messages. The event-handler will
// process both the vrouter response messages
//
// The flow-handle and gen-id are got from base class (FlowEvent)
////////////////////////////////////////////////////////////////////////////
class FlowEventKSync : public FlowEvent {
public:
    FlowEventKSync(const KSyncEntry::KSyncEntryPtr ksync_entry,
                   KSyncEntry::KSyncEvent ksync_event, uint32_t flow_handle,
                   uint32_t gen_id, int ksync_error, uint64_t evict_flow_bytes,
                   uint64_t evict_flow_packets, uint64_t evict_flow_oflow) :
        FlowEvent(KSYNC_EVENT, NULL, flow_handle, gen_id),
        ksync_entry_(ksync_entry), ksync_event_(ksync_event),
        ksync_error_(ksync_error), evict_flow_bytes_(evict_flow_bytes),
        evict_flow_packets_(evict_flow_packets),
        evict_flow_oflow_(evict_flow_oflow) {
    }

    FlowEventKSync(const FlowEventKSync &rhs) :
        FlowEvent(rhs), ksync_entry_(rhs.ksync_entry_),
        ksync_event_(rhs.ksync_event_), ksync_error_(rhs.ksync_error_),
        evict_flow_bytes_(rhs.evict_flow_bytes_),
        evict_flow_packets_(rhs.evict_flow_packets_),
        evict_flow_oflow_(rhs.evict_flow_oflow_) {
    }

    virtual ~FlowEventKSync() { }

    KSyncEntry *ksync_entry() const { return ksync_entry_.get(); }
    KSyncEntry::KSyncEvent ksync_event() const { return ksync_event_; }
    int ksync_error() const { return ksync_error_; }
    uint64_t evict_flow_bytes() const { return evict_flow_bytes_; }
    uint64_t evict_flow_packets() const { return evict_flow_packets_; }
    uint64_t evict_flow_oflow() const { return evict_flow_oflow_; }
private:
    KSyncEntry::KSyncEntryPtr ksync_entry_;
    KSyncEntry::KSyncEvent ksync_event_;
    int ksync_error_;
    uint64_t evict_flow_bytes_;
    uint64_t evict_flow_packets_;
    uint64_t evict_flow_oflow_;
};

////////////////////////////////////////////////////////////////////////////
// FlowProto uses following queues,
//
// - FlowEventQueue
//   This queue contains events for flow add, flow eviction etc...
//   See FlowProto::FlowEventHandler for events handled in this queue
// - KSyncFlowEventQueue
//   This queue contains events generated from KSync response for a flow
// - DeleteFlowEventQueue
//   This queue contains events generated for flow-ageing
// - UpdateFlowEventQueue
//   This queue contains events generated as result of config changes such
//   as add/delete/change of interface, vn, vm, acl, nh, route etc...
//
// All queues are defined from a base class FlowEventQueueBase.
// FlowEventQueueBase implements a wrapper around the WorkQueues with following
// additional functionality,
//
// - Rate Control using Tokens
//   All the queues give above can potentially add/change/delete flows in the
//   vrouter. So, the queues given above acts as producer and VRouter acts as
//   consumer. VRouter is a slow consumer of events. To provide fairness
//   across queues, a "token" based scheme is used. See flow_token.h for more
//   information
//
//   The queue will stop the WorkQueue when it runs out of tokens. The queue
//   is started again after a minimum number of tokens become available
//
// - Time limits
//   Intermittently, it is observed that some of the queues take large amount
//   of time. Latencies in queue such as KSync queue or delete-queue can result
//   in flow-setup latencies. So, we want to impose an upper bound on the
//   amount of time taken in single run of WorkQueue.
//
//   We take timestamp at start of queue, and check latency for every 8
//   events processed in the queue. If the latency goes beyond a limit, the
//   WorkQueue run is aborted.
////////////////////////////////////////////////////////////////////////////
class FlowEventQueueBase {
public:
    typedef WorkQueue<FlowEvent *> Queue;

    FlowEventQueueBase(FlowProto *proto, const std::string &name,
                       uint32_t task_id, int task_instance,
                       FlowTokenPool *pool, uint16_t latency_limit,
                       uint32_t max_iterations);
    virtual ~FlowEventQueueBase();
    virtual bool HandleEvent(FlowEvent *event) = 0;
    virtual bool Handler(FlowEvent *event);

    void Shutdown();
    void Enqueue(FlowEvent *event);
    bool TokenCheck();
    bool TaskEntry();
    void TaskExit(bool done);
    void set_disable(bool val) { queue_->set_disable(val); }
    uint32_t Length() { return queue_->Length(); }
    void MayBeStartRunner() { queue_->MayBeStartRunner(); }
    Queue *queue() const { return queue_; }
    uint64_t events_processed() const { return events_processed_; }
    uint64_t events_enqueued() const { return queue_->NumEnqueues(); }

protected:
    bool CanEnqueue(FlowEvent *event);
    bool CanProcess(FlowEvent *event);
    void ProcessDone(FlowEvent *event, bool update_rev_flow);

    Queue *queue_;
    FlowProto *flow_proto_;
    FlowTokenPool *token_pool_;
    uint64_t task_start_;
    // Number of entries processed in single run of WorkQueue
    uint32_t count_;
    // Number of events processed. Skips event that are state-compressed
    // due to Flow PendingActions
    uint64_t events_processed_;
    uint16_t latency_limit_;
    struct rusage rusage_;
};

class FlowEventQueue : public FlowEventQueueBase {
public:
    FlowEventQueue(Agent *agent, FlowProto *proto, FlowTable *table,
                   FlowTokenPool *pool, uint16_t latency_limit,
                   uint32_t max_iterations);
    virtual ~FlowEventQueue();

    bool HandleEvent(FlowEvent *event);
private:
    FlowTable *flow_table_;
};

class DeleteFlowEventQueue : public FlowEventQueueBase {
public:
    DeleteFlowEventQueue(Agent *agent, FlowProto *proto, FlowTable *table,
                         FlowTokenPool *pool, uint16_t latency_limit,
                         uint32_t max_iterations);
    virtual ~DeleteFlowEventQueue();

    bool HandleEvent(FlowEvent *event);
private:
    FlowTable *flow_table_;
};

class KSyncFlowEventQueue : public FlowEventQueueBase {
public:
    KSyncFlowEventQueue(Agent *agent, FlowProto *proto, FlowTable *table,
                        FlowTokenPool *pool, uint16_t latency_limit,
                        uint32_t max_iterations);
    virtual ~KSyncFlowEventQueue();

    bool HandleEvent(FlowEvent *event);
private:
    FlowTable *flow_table_;
};

class UpdateFlowEventQueue : public FlowEventQueueBase {
public:
    UpdateFlowEventQueue(Agent *agent, FlowProto *proto,
                         FlowTokenPool *pool, uint16_t latency_limit,
                         uint32_t max_iterations);
    virtual ~UpdateFlowEventQueue();

    bool HandleEvent(FlowEvent *event);
};

#endif //  __AGENT_FLOW_EVENT_H__
