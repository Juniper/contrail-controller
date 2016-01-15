/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EXPORT_REQUEST_H__
#define __AGENT_FLOW_EXPORT_REQUEST_H__

#include "pkt/flow_table.h"
#include "pkt/flow_entry.h"
#include "vrouter/flow_stats/flow_export_info.h"

////////////////////////////////////////////////////////////////////////////
// Request to the Flow StatsCollector module
////////////////////////////////////////////////////////////////////////////
class FlowExportReq {
public:
    enum Event {
        INVALID,
        ADD_FLOW,
        DELETE_FLOW,
        UPDATE_FLOW_INDEX,
        UPDATE_FLOW_STATS
    };

    FlowExportReq(Event event, const FlowKey &key, FlowExportInfo info) :
        event_(event), key_(key), info_(info), time_(0), index_(0) { 
    }

    FlowExportReq(Event event, const FlowKey &key, uint64_t time) : 
        event_(event), key_(key), time_(time), index_(0) { 
    }

    FlowExportReq(Event event, const FlowKey &key, uint64_t t, uint32_t idx) :
        event_(event), key_(key), time_(t), index_(idx) { 
    }

    FlowExportReq(Event event, const FlowKey &key, uint32_t bytes,
                  uint32_t packets, uint32_t oflow_bytes) :
                  event_(event), key_(key), bytes_(bytes), packets_(packets),
                  oflow_bytes_(oflow_bytes) {
    }

    ~FlowExportReq() { }

    Event event() const { return event_; }
    const FlowKey &key() const { return key_; }
    FlowExportInfo info() const { return info_; }
    uint64_t time() const { return time_; }
    uint32_t index() const  { return index_; }
    uint32_t bytes() const { return bytes_;}
    uint32_t packets() const { return packets_;}
    uint32_t oflow_bytes() const { return oflow_bytes_;}

private:
    Event event_;
    const FlowKey key_;
    FlowExportInfo info_;
    uint64_t time_;
    uint32_t index_;
    uint32_t bytes_;
    uint32_t packets_;
    uint32_t oflow_bytes_;
    DISALLOW_COPY_AND_ASSIGN(FlowExportReq);
};
#endif //  __AGENT_FLOW_EXPORT_REQUEST_H__
