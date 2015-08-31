/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EXPORT_PARAMS_H__
#define __AGENT_FLOW_EXPORT_PARAMS_H__

enum RevFlowParamFlags {
    UuidSet         = 1 << 0,
    DstIpSet        = 1 << 1
};

struct RevFlowParams {
    uint32_t flags;
    uint32_t rev_flow_dst_ip; /* Will be used to override SIP for flow */
    uuid rev_flow_uuid;

    RevFlowParams() : flags(0) {
    }

    bool is_flags_set(const RevFlowParamFlags &value) const {
        return (flags & value);
    }
    void set_flags(const RevFlowParamFlags &value) { flags |= value; }
};

struct FlowExportParams {
    uint64_t diff_bytes;
    uint64_t diff_packets;
    uint64_t teardown_time;
    RevFlowParams rev_flow_params;
    FlowExportParams() : diff_bytes(0), diff_packets(0), teardown_time(0),
        rev_flow_params() {
    }
    FlowExportParams(uint64_t bytes, uint64_t pkts) : diff_bytes(bytes),
        diff_packets(pkts), teardown_time(0), rev_flow_params() {
    }
};

#endif //  __AGENT_FLOW_EXPORT_PARAMS_H__
