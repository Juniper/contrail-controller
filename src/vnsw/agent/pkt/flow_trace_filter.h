#ifndef __AGENT_FLOW_TRACE_FILTER_H__
#define __AGENT_FLOW_TRACE_FILTER_H__
#include <net/address_util.h>

class FlowKey;
class SandeshFlowFilterInfo;

/////////////////////////////////////////////////////////////////////////////
// This class provides filtering for flow trace messages. In scaled environment
// tracing has following problems,
//
// 1. The trace-buffer is limited, so the messages overflow buffer very quick.
//    Its difficult to catch traces for flows of interest
// 2. Logging adds latency and becomes a bottleneck
//
// Flow filtering provides mechanism allows to filter and trace only flows of
// interest.
//
// By default, filtering allows all flows to be traced. Tracing can be
// configured from introspect
/////////////////////////////////////////////////////////////////////////////
struct FlowTraceFilter {
    bool enabled_;
    Address::Family family_;
    IpAddress src_addr_;
    IpAddress src_mask_;
    IpAddress dst_addr_;
    IpAddress dst_mask_;
    uint8_t proto_start_;
    uint8_t proto_end_;
    uint16_t src_port_start_;
    uint16_t src_port_end_;
    uint16_t dst_port_start_;
    uint16_t dst_port_end_;
    // Number of successful calls to Match API. Note, this is not same as
    // number of flows traced. A flow can call Match multiple times and the
    // counter is incremented every-time
    tbb::atomic<uint64_t> count_;

    FlowTraceFilter();
    ~FlowTraceFilter() { }

    void Init(bool enable, Address::Family family);
    void Reset(bool enable, Address::Family family);

    void SetFilter(bool enable, Address::Family family,
                   const std::string &src_addr, uint8_t src_plen,
                   const std::string &dst_addr, uint8_t dst_plen,
                   uint8_t proto_start, uint8_t proto_end,
                   uint16_t src_port_start, uint16_t src_port_end,
                   uint16_t dst_port_start, uint16_t dst_port_end);
    bool Match(const FlowKey *key);
    void ToSandesh(SandeshFlowFilterInfo *info) const;
};

#endif // __AGENT_FLOW_TRACE_FILTER_H__
