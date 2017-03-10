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
        UPDATE_FLOW_STATS
    };

    FlowExportReq(Event event, const FlowExportInfo &info,
                  const PreviousFlowVnInfo &prev_vn) :
        event_(event), info_(info), time_(0), prev_flow_vn_info_(prev_vn) {
    }

    FlowExportReq(Event event, const FlowExportInfo &info, uint64_t time,
                  const PreviousFlowVnInfo &prev_vn,
                  const RevFlowDepParams &p) :
        event_(event), info_(info), time_(time), params_(p),
        prev_flow_vn_info_(prev_vn) {
    }

    FlowExportReq(Event event, const FlowExportInfo &info, uint32_t bytes,
                  uint32_t packets, uint32_t oflow_bytes,
                  const PreviousFlowVnInfo &prev_vn) :
                  event_(event), info_(info), bytes_(bytes), packets_(packets),
                  oflow_bytes_(oflow_bytes), prev_flow_vn_info_(prev_vn) {
    }

    ~FlowExportReq() { }

    Event event() const { return event_; }
    FlowExportInfo info() const { return info_; }
    uint64_t time() const { return time_; }
    uint32_t bytes() const { return bytes_;}
    uint32_t packets() const { return packets_;}
    uint32_t oflow_bytes() const { return oflow_bytes_;}
    const RevFlowDepParams& params() const { return params_; }
    const PreviousFlowVnInfo& prev_flow_vn_info() const {
        return prev_flow_vn_info_;
    }

private:
    Event event_;
    FlowExportInfo info_;
    uint64_t time_;
    uint32_t bytes_;
    uint32_t packets_;
    uint32_t oflow_bytes_;
    RevFlowDepParams params_;
    PreviousFlowVnInfo prev_flow_vn_info_;
    DISALLOW_COPY_AND_ASSIGN(FlowExportReq);
};
#endif //  __AGENT_FLOW_EXPORT_REQUEST_H__
