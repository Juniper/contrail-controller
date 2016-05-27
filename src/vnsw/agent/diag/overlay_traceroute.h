/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_overlay_trace_route_hpp
#define vnsw_agent_diag_overlay_trace_route_hpp

#include "diag/diag.h"
#include "diag/diag_types.h"
#include "pkt/control_interface.h"
class DiagTable;
class OverlayTraceRoute: public DiagEntry {
public:
    OverlayTraceRoute(const OverlayTraceReq *req, DiagTable *diag_table);
    virtual ~OverlayTraceRoute();
    virtual void SendRequest();
    void RequestTimedOut(uint32_t seqno);
    virtual void HandleReply(DiagPktHandler *handler);
    void ReplyLocalHop();
    virtual bool IsDone() { return done_;}
private:
    void IncrementTtl();
    uuid  vn_uuid_;
    MacAddress remote_vm_mac_;
    uint8_t ttl_;
    bool       done_;
    uint16_t   max_ttl_;
    uint32_t len_;
    std::string context_;
};
#endif
