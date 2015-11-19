/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_
#define SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_

#include <sandesh/sandesh_trace.h>

class BgpServer;

class RoutingPolicyMgr {
public:
    RoutingPolicyMgr(BgpServer *server);
    virtual ~RoutingPolicyMgr();
    SandeshTraceBufferPtr trace_buffer() const { return trace_buf_; }

private:
    BgpServer *server() { return server_; }
    const BgpServer *server() const { return server_; };
    BgpServer *server_;
    SandeshTraceBufferPtr trace_buf_;
};
#endif // SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_


