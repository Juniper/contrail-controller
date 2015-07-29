/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_MGMT_RESPONSE_H__
#define __AGENT_FLOW_MGMT_RESPONSE_H__

#include "pkt/flow_table.h"

////////////////////////////////////////////////////////////////////////////
// Response events generated from Flow Management module
////////////////////////////////////////////////////////////////////////////
class FlowMgmtResponse {
public:
    enum Event {
        INVALID,
        FREE_FLOW_REF,
        REVALUATE_FLOW,
        REVALUATE_DBENTRY,
        DELETE_DBENTRY,
        FREE_DBENTRY
    };

    FlowMgmtResponse() :
        event_(INVALID), flow_(NULL), db_entry_(NULL), gen_id_(0) {
    }

    FlowMgmtResponse(Event event, FlowEntry *flow) :
        event_(event), flow_(flow), db_entry_(NULL), gen_id_(0) {
    }

    FlowMgmtResponse(Event event, FlowEntry *flow, const DBEntry *db_entry) :
        event_(event), flow_(flow), db_entry_(db_entry), gen_id_(0) {
    }

    FlowMgmtResponse(Event event, const DBEntry *db_entry, uint32_t gen_id) :
        event_(event), flow_(NULL), db_entry_(db_entry), gen_id_(gen_id) {
    }

    FlowMgmtResponse(const FlowMgmtResponse &rhs) :
        event_(rhs.event_), flow_(rhs.flow()), db_entry_(rhs.db_entry_),
        gen_id_(rhs.gen_id_) {
    }

    virtual ~FlowMgmtResponse() { }

    Event event() const { return event_; }
    FlowEntry *flow() const { return flow_.get(); }
    void set_flow(FlowEntry *flow) { flow_ = flow; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }
    uint32_t gen_id() const { return gen_id_; }

private:
    Event event_;
    FlowEntryPtr flow_;
    const DBEntry *db_entry_;
    uint32_t gen_id_;
};

#endif //  __AGENT_FLOW_MGMT_RESPONSE_H__
