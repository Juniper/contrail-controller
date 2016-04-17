/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EVENT_H__
#define __AGENT_FLOW_EVENT_H__

#include <ksync/ksync_entry.h>
#include "flow_table.h"

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
        // prefix. The event will re-valuate the complete flow due to change
        // in route used for flow
        REVALUATE_FLOW,
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
    };

    FlowEvent() :
        event_(INVALID), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false), table_index_(0) {
    }

    FlowEvent(Event event, FlowEntry *flow) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0){
    }

    FlowEvent(Event event, FlowEntry *flow, uint32_t flow_handle,
              uint8_t gen_id) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        gen_id_(gen_id), del_rev_flow_(false), flow_handle_(flow_handle),
        table_index_(0) {
    }

    FlowEvent(Event event, FlowEntry *flow, const DBEntry *db_entry) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(db_entry),
        gen_id_(0), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, const DBEntry *db_entry, uint32_t gen_id) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(db_entry),
        gen_id_(gen_id), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, const FlowKey &key, bool del_rev_flow,
              uint32_t table_index) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), flow_key_(key), del_rev_flow_(del_rev_flow),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(table_index) {
    }

    FlowEvent(Event event, const FlowKey &key) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), flow_key_(key), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, const FlowKey &key, uint32_t flow_handle,
              uint8_t gen_id) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(gen_id), flow_key_(key), del_rev_flow_(false),
        flow_handle_(flow_handle), table_index_(0) {
    }

    FlowEvent(Event event, PktInfoPtr pkt_info) :
        event_(event), flow_(NULL), pkt_info_(pkt_info), db_entry_(NULL),
        gen_id_(0), flow_key_(), del_rev_flow_(),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(0) {
    }

    FlowEvent(Event event, PktInfoPtr pkt_info, uint8_t table_index) :
        event_(event), flow_(NULL), pkt_info_(pkt_info), db_entry_(NULL),
        gen_id_(0), flow_key_(), del_rev_flow_(),
        flow_handle_(FlowEntry::kInvalidFlowHandle), table_index_(table_index) {
    }

    FlowEvent(const FlowEvent &rhs) :
        event_(rhs.event_), flow_(rhs.flow()), pkt_info_(rhs.pkt_info_),
        db_entry_(rhs.db_entry_), gen_id_(rhs.gen_id_),
        flow_key_(rhs.flow_key_), del_rev_flow_(rhs.del_rev_flow_),
        flow_handle_(rhs.flow_handle_), table_index_(rhs.table_index_) {
    }

    virtual ~FlowEvent() { }

    Event event() const { return event_; }
    FlowEntry *flow() const { return flow_.get(); }
    FlowEntryPtr &flow_ref() { return flow_; }
    void set_flow(FlowEntry *flow) { flow_ = flow; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }
    uint32_t gen_id() const { return gen_id_; }
    const FlowKey &get_flow_key() const { return flow_key_; }
    bool get_del_rev_flow() const { return del_rev_flow_; }
    PktInfoPtr pkt_info() const { return pkt_info_; }
    uint32_t flow_handle() const { return flow_handle_; }
    uint32_t table_index() const { return table_index_;}
private:
    Event event_;
    FlowEntryPtr flow_;
    PktInfoPtr pkt_info_;
    const DBEntry *db_entry_;
    uint32_t gen_id_;
    FlowKey flow_key_;
    bool del_rev_flow_;
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

#endif //  __AGENT_FLOW_EVENT_H__
