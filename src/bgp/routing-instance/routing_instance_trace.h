/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>

#include "bgp/routing-instance/routing_instance_analytics_types.h"

//
// RoutingInstance trace macros. Optionally logs the server name as well for
// easier debugging in multiple server unit tests.
//
#define ROUTING_INSTANCE_TRACE_INTERNAL(obj, server, trace_buf, ...)           \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        bgp_log_test::LogServerName(server);                                   \
        RoutingInstance##obj##Log::Send(g_vns_constants.CategoryNames.find(    \
                                   Category::ROUTING_INSTANCE)->second,        \
                                   SandeshLevel::SYS_DEBUG, __FILE__, __LINE__,\
                                   ##__VA_ARGS__);                             \
        RoutingInstance##obj::TraceMsg((trace_buf), __FILE__, __LINE__,        \
                                       ##__VA_ARGS__);                         \
    } while (false)

#define ROUTING_INSTANCE_TRACE(obj, server, ...)                               \
    ROUTING_INSTANCE_TRACE_INTERNAL(obj, server,                               \
        (server)->routing_instance_mgr()->trace_buffer(), ##__VA_ARGS__)

#define ROUTING_INSTANCE_DELETE_ACTOR_TRACE(obj, server, ...)                  \
    ROUTING_INSTANCE_TRACE_INTERNAL(obj, server,                               \
        (server)->routing_instance_mgr()->trace_buffer(), ##__VA_ARGS__)

#define ROUTING_INSTANCE_MGR_TRACE(obj, server, ...)                           \
    ROUTING_INSTANCE_TRACE_INTERNAL(obj, server, trace_buf_, ##__VA_ARGS__)

#define ROUTING_INSTANCE_COLLECTOR_INFO(info)                                  \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    ROUTING_INSTANCE_COLLECTOR_SEND(info);                                     \
} while (false)
