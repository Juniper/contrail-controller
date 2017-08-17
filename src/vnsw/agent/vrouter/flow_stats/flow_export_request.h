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

    FlowExportReq(Event event, const FlowExportInfo &info) :
        event_(event), info_(info), time_(0) {
    }

    FlowExportReq(Event event, const FlowExportInfo &info, uint64_t time,
                  const RevFlowDepParams &p) :
        event_(event), info_(info), time_(time), params_(p) {
    }

    FlowExportReq(Event event, const FlowExportInfo &info, uint32_t bytes,
                  uint32_t packets, uint32_t oflow_bytes, uint32_t mir_bytes,
                  uint32_t mir_packets, uint32_t mir_oflow_bytes, uint32_t sec_mir_bytes, uint32_t sec_mir_packets, uint32_t sec_mir_oflow_bytes,
                  const boost::uuids::uuid &u) :
                  event_(event), info_(info), bytes_(bytes), packets_(packets),
                  oflow_bytes_(oflow_bytes), mir_bytes_(mir_bytes),
                  mir_packets_(mir_packets), mir_oflow_bytes_(mir_oflow_bytes),
                  sec_mir_bytes_(mir_bytes), sec_mir_packets_(mir_packets),
                  sec_mir_oflow_bytes_(mir_oflow_bytes),uuid_(u) {
    }

    ~FlowExportReq() { }

    Event event() const { return event_; }
    FlowExportInfo info() const { return info_; }
    uint64_t time() const { return time_; }
    uint32_t bytes() const { return bytes_;}
    uint32_t packets() const { return packets_;}
    uint32_t oflow_bytes() const { return oflow_bytes_;}
    uint32_t mir_bytes() const { return mir_bytes_;}
    uint32_t mir_packets() const { return mir_packets_;}
    uint32_t mir_oflow_bytes() const { return mir_oflow_bytes_;}
    uint32_t sec_mir_bytes() const { return sec_mir_bytes_;}
    uint32_t sec_mir_packets() const { return sec_mir_packets_;}
    uint32_t sec_mir_oflow_bytes() const { return sec_mir_oflow_bytes_;}
    const RevFlowDepParams& params() const { return params_; }
    boost::uuids::uuid uuid() const { return uuid_; }

private:
    Event event_;
    FlowExportInfo info_;
    uint64_t time_;
    uint32_t bytes_;
    uint32_t packets_;
    uint32_t oflow_bytes_;
    uint32_t mir_bytes_;
    uint32_t mir_packets_;
    uint32_t mir_oflow_bytes_;
    uint32_t sec_mir_bytes_;
    uint32_t sec_mir_packets_;
    uint32_t sec_mir_oflow_bytes_;
    RevFlowDepParams params_;
    boost::uuids::uuid uuid_;
    DISALLOW_COPY_AND_ASSIGN(FlowExportReq);
};
#endif //  __AGENT_FLOW_EXPORT_REQUEST_H__
