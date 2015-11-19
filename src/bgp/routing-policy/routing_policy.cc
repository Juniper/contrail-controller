/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy.h"


RoutingPolicyMgr::RoutingPolicyMgr(BgpServer *server)
    : server_(server),
      trace_buf_(SandeshTraceBufferCreate("RoutingPolicyMgr", 500)) {
}

RoutingPolicyMgr::~RoutingPolicyMgr() {
}


