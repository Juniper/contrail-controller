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
        UPDATE_FLOW_STATS,
    };

    FlowExportReq(Event event, FlowEntry *ptr) :
        event_(event), flow_(ptr), time_(0){
    }

    FlowExportReq(Event event, FlowEntry *ptr, uint64_t time) :
        event_(event), flow_(ptr), time_(time) {
    }

    FlowExportReq(Event event, FlowEntry *ptr,
                  uint32_t bytes, uint32_t packets, uint32_t oflow_bytes):
                  event_(event), flow_(ptr), bytes_(bytes),
                  packets_(packets), oflow_bytes_(oflow_bytes) {
    }

    ~FlowExportReq() { }

    Event event() const { return event_; }
    FlowEntry* flow() const { return flow_.get(); }
    uint64_t time() const { return time_; }
    uint32_t bytes() const { return bytes_;}
    uint32_t packets() const { return packets_;}
    uint32_t oflow_bytes() const { return oflow_bytes_;}

private:
    Event event_;
    FlowEntryPtr flow_;
    uint64_t time_;
    uint32_t bytes_;
    uint32_t packets_;
    uint32_t oflow_bytes_;

    DISALLOW_COPY_AND_ASSIGN(FlowExportReq);
};
#endif //  __AGENT_FLOW_EXPORT_REQUEST_H__
