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
        // Flow was waiting to index to be free. Event to specify that flow
        // should retry to acquire index
        RETRY_INDEX_ACQUIRE,
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
        // Set flow-handle for a flow
        FLOW_HANDLE_UPDATE,
        // Error by vrouter for the flow
        KSYNC_VROUTER_ERROR,
        // Generate KSync event for the flow
        KSYNC_EVENT,
    };

    FlowEvent() :
        event_(INVALID), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
        ksync_event_() {
    }

    FlowEvent(Event event) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false), ksync_entry_(NULL), ksync_event_() {
    }

    FlowEvent(Event event, FlowEntry *flow) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
        ksync_event_() {
    }

    FlowEvent(Event event, FlowEntry *flow, uint32_t flow_handle) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(NULL),
        gen_id_(0), del_rev_flow_(false), flow_handle_(flow_handle),
        ksync_entry_(NULL), ksync_event_() {
    }

    FlowEvent(Event event, FlowEntry *flow, const DBEntry *db_entry) :
        event_(event), flow_(flow), pkt_info_(), db_entry_(db_entry),
        gen_id_(0), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
        ksync_event_() {
    }

    FlowEvent(Event event, const DBEntry *db_entry, uint32_t gen_id) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(db_entry),
        gen_id_(gen_id), del_rev_flow_(false),
        flow_handle_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
        ksync_event_() {
    }

    FlowEvent(Event event, const FlowKey &key, bool del_rev_flow) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), flow_key_(key), del_rev_flow_(del_rev_flow),
        flow_handle_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
        ksync_event_() {
    }

    FlowEvent(Event event, const FlowKey &key, uint32_t flow_handle) :
        event_(event), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), flow_key_(key), del_rev_flow_(false),
        flow_handle_(flow_handle), ksync_entry_(NULL), ksync_event_() {
    }

    FlowEvent(Event event, PktInfoPtr pkt_info) :
        event_(event), flow_(NULL), pkt_info_(pkt_info), db_entry_(NULL),
        gen_id_(0), flow_key_(), del_rev_flow_(),
        flow_handle_(FlowEntry::kInvalidFlowHandle),
        ksync_entry_(NULL), ksync_event_() {
    }

    FlowEvent(KSyncEntry *entry, KSyncEntry::KSyncEvent event) :
        event_(KSYNC_EVENT), flow_(NULL), pkt_info_(), db_entry_(NULL),
        gen_id_(0), flow_key_(), del_rev_flow_(),
        flow_handle_(FlowEntry::kInvalidFlowHandle),
        ksync_entry_(entry), ksync_event_(event) {
    }

    FlowEvent(KSyncEntry *entry, uint32_t flow_handle) :
        event_(FLOW_HANDLE_UPDATE), flow_(NULL), pkt_info_(),
        db_entry_(NULL), gen_id_(0), flow_key_(), del_rev_flow_(),
        flow_handle_(flow_handle), ksync_entry_(entry), ksync_event_() {
    }

    FlowEvent(KSyncEntry *entry) :
        event_(KSYNC_VROUTER_ERROR), flow_(NULL), pkt_info_(),
        db_entry_(NULL), gen_id_(0), flow_key_(), del_rev_flow_(),
        flow_handle_(FlowEntry::kInvalidFlowHandle), ksync_entry_(entry),
        ksync_event_() {
    }

    FlowEvent(const FlowEvent &rhs) :
        event_(rhs.event_), flow_(rhs.flow()), pkt_info_(rhs.pkt_info_),
        db_entry_(rhs.db_entry_), gen_id_(rhs.gen_id_),
        flow_key_(rhs.flow_key_), del_rev_flow_(rhs.del_rev_flow_),
        flow_handle_(rhs.flow_handle_),
        ksync_entry_(rhs.ksync_entry_), ksync_event_(rhs.ksync_event_) {
    }

    virtual ~FlowEvent() { }

    Event event() const { return event_; }
    FlowEntry *flow() const { return flow_.get(); }
    void set_flow(FlowEntry *flow) { flow_ = flow; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }
    uint32_t gen_id() const { return gen_id_; }
    const FlowKey &get_flow_key() const { return flow_key_; }
    bool get_del_rev_flow() const { return del_rev_flow_; }
    PktInfoPtr pkt_info() const { return pkt_info_; }
    uint32_t flow_handle() const { return flow_handle_; }

    KSyncEntry *ksync_entry() const { return ksync_entry_.get(); }
    KSyncEntry::KSyncEvent ksync_event() const { return ksync_event_; }
private:
    Event event_;
    FlowEntryPtr flow_;
    PktInfoPtr pkt_info_;
    const DBEntry *db_entry_;
    uint32_t gen_id_;
    FlowKey flow_key_;
    bool del_rev_flow_;
    uint32_t flow_handle_;
    KSyncEntry::KSyncEntryPtr ksync_entry_;
    KSyncEntry::KSyncEvent ksync_event_;
};

#endif //  __AGENT_FLOW_EVENT_H__
