/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EXPORT_REQUEST_H__
#define __AGENT_FLOW_EXPORT_REQUEST_H__

#include "pkt/flow_table.h"

////////////////////////////////////////////////////////////////////////////
// Request to the Flow StatsCollector module
////////////////////////////////////////////////////////////////////////////
class FlowExportReq {
public:
    enum Event {
        INVALID,
        ADD_FLOW,
        DELETE_FLOW,
        UPDATE_FLOW_INDEX
    };

    FlowExportReq(Event event, FlowEntry *ptr) :
        event_(event), flow_(ptr), time_(0){
    }

    FlowExportReq(Event event, FlowEntry *ptr, uint64_t time) :
        event_(event), flow_(ptr), time_(time) {
    }

    ~FlowExportReq() { }

    Event event() const { return event_; }
    FlowEntry* flow() const { return flow_.get(); }
    uint64_t time() const { return time_; }

private:
    Event event_;
    FlowEntryPtr flow_;
    uint64_t time_;

    DISALLOW_COPY_AND_ASSIGN(FlowExportReq);
};
#endif //  __AGENT_FLOW_EXPORT_REQUEST_H__
