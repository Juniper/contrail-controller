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

#define RTINSTANCE_TRACE_BUF "RoutingInstanceTraceBuf"
extern SandeshTraceBufferPtr RoutingInstanceTraceBuf;

#define RTINSTANCE_LOG_FLAG_SYSLOG 1
#define RTINSTANCE_LOG_FLAG_TRACE  2
#define RTINSTANCE_LOG_FLAG_ALL    (RTINSTANCE_LOG_FLAG_SYSLOG |               \
                                    RTINSTANCE_LOG_FLAG_TRACE)                 \

#define RTINSTANCE_LOG_INTERNAL(type, server, level, flags, ...)               \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    if ((flags) & RTINSTANCE_LOG_FLAG_SYSLOG) {                                \
        bgp_log_test::LogServerName(server);                                   \
        RoutingInstance##type##Log::Send(g_vns_constants.CategoryNames.find(   \
                                    Category::ROUTING_INSTANCE)->second,       \
                                    level, __FILE__, __LINE__, ##__VA_ARGS__); \
    }                                                                          \
    if ((flags) & RTINSTANCE_LOG_FLAG_TRACE) {                                 \
        const std::string __trace_buf(RTINSTANCE_TRACE_BUF);             \
        RoutingInstance##type::TraceMsg(RoutingInstanceTraceBuf,               \
                                        __FILE__, __LINE__, ##__VA_ARGS__);    \
    }                                                                          \
} while (false)                                                                \

#define RTINSTANCE_LOG(type, rtinstance, level, flags, ...)                    \
    RTINSTANCE_LOG_INTERNAL(type, (rtinstance)->server(), level, flags,        \
                            (rtinstance)->name(), ##__VA_ARGS__);              \

#define RTINSTANCE_LOG_MESSAGE(server, level, flags, ...)                      \
    RTINSTANCE_LOG_INTERNAL(Message, server, level, flags, ##__VA_ARGS__);     \

#define RTINSTANCE_LOG_PEER(type, rtinstance, peer, level, flags, ...)         \
    RTINSTANCE_LOG_INTERNAL(Peer##type, (rtinstance)->server(), level, flags,  \
                            (rtinstance)->name(),                              \
                            (peer)->peer_key().endpoint.address().to_string(), \
                            ##__VA_ARGS__);                                    \

#define RTINSTANCE_LOG_TABLE(type, rtinstance, table, level, flags, ...)       \
    RTINSTANCE_LOG_INTERNAL(Table##type, (rtinstance)->server(), level, flags, \
                            (rtinstance)->name(), (table)->name(),             \
                            Address::FamilyToString((table)->family()),        \
                            ##__VA_ARGS__);                                    \

#define ROUTING_INSTANCE_COLLECTOR_INFO(info)                                  \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    ROUTING_INSTANCE_COLLECTOR_SEND(info);                                     \
} while (false)
