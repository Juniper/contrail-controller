/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IO_LOG_H__

#define __IO_LOG_H__

#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "io/io_log_types.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"

#define IO_TRACE_BUF       "IOTraceBuf"
#define IO_LOG_FLAG_SYSLOG 1
#define IO_LOG_FLAG_TRACE  2
#define IO_LOG_FLAG_ALL    (IO_LOG_FLAG_SYSLOG | IO_LOG_FLAG_TRACE)

extern SandeshTraceBufferPtr IOTraceBuf;

#define IO_LOG(obj, level, flags, ...)                                         \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    if ((flags) & IO_LOG_FLAG_SYSLOG) {                                        \
        obj##Log::Send(g_vns_constants.CategoryNames.find(Category::TCP)->second,\
                       level, __FILE__, __LINE__, ##__VA_ARGS__);              \
    }                                                                          \
    if ((flags) & IO_LOG_FLAG_TRACE) {                                         \
        obj##Trace::TraceMsg(IOTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);   \
    }                                                                          \
} while (false)

//
// TCP Log and Trace macros
//
#define TCP_DIR_OUT ">"
#define TCP_DIR_IN  "<"
#define TCP_DIR_NA  ""

//
// Base macros to log and/or trace TCP messages with C++ string as last arg.
//
#define TCP_SERVER_LOG_STR(obj, level, flags, server, dir, arg)                \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        if ((server) && (server)->DisableSandeshLogMessages()) {               \
            LOG(DEBUG, "Server " << (server)->ToString() << dir << " " << arg);\
            break;                                                             \
        }                                                                      \
        std::ostringstream out;                                                \
        out << arg;                                                            \
        IO_LOG(TcpServerMessage, level, flags,                                 \
               (server) ? (server)->ToString() : "", dir, out.str());          \
    } while (false)

#define TCP_SESSION_LOG_STR(obj, level, flags, session, dir, arg)              \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        if ((session) && (session)->server() &&                                \
            (session)->server()->DisableSandeshLogMessages()) {                \
            LOG(DEBUG, "Session " << (session)->ToString() << dir<< " "<< arg);\
            break;                                                             \
        }                                                                      \
        std::ostringstream out;                                                \
        out << arg;                                                            \
        IO_LOG(TcpSessionMessage, level, flags,                                \
               (session) ? (session)->ToString() : "", dir, out.str());        \
    } while (false)

#define TCP_SERVER_LOG_ERROR(server, dir, arg)                                 \
    TCP_SERVER_LOG_STR(TcpServerMessage, SandeshLevel::SYS_ERR,                \
                       IO_LOG_FLAG_ALL, server, dir, arg)
#define TCP_SERVER_LOG_INFO(server, dir, arg)                                  \
    TCP_SERVER_LOG_STR(TcpServerMessage, SandeshLevel::SYS_INFO,               \
                       IO_LOG_FLAG_SYSLOG, server, dir, arg)
#define TCP_SERVER_LOG_DEBUG(server, dir, arg)                                 \
    TCP_SERVER_LOG_STR(TcpServerMessage, SandeshLevel::SYS_DEBUG,              \
                       IO_LOG_FLAG_SYSLOG, server, dir, arg)
#define TCP_SERVER_LOG_UT_DEBUG(server, dir, arg)                              \
    TCP_SERVER_LOG_STR(TcpServerMessage, SandeshLevel::UT_DEBUG,               \
                       IO_LOG_FLAG_SYSLOG, server, dir, arg)

#define TCP_SESSION_LOG_ERROR(session, dir, arg)                               \
    TCP_SESSION_LOG_STR(TcpSessionMessage, SandeshLevel::SYS_ERR,              \
                       IO_LOG_FLAG_ALL, session, dir, arg)
#define TCP_SESSION_LOG_INFO(session, dir, arg)                                \
    TCP_SESSION_LOG_STR(TcpSessionMessage, SandeshLevel::SYS_INFO,             \
                       IO_LOG_FLAG_SYSLOG, session, dir, arg)
#define TCP_SESSION_LOG_DEBUG(session, dir, arg)                               \
    TCP_SESSION_LOG_STR(TcpSessionMessage, SandeshLevel::SYS_DEBUG,            \
                       IO_LOG_FLAG_SYSLOG, session, dir, arg)
#define TCP_SESSION_LOG_UT_DEBUG(session, dir, arg)                            \
    TCP_SESSION_LOG_STR(TcpSessionMessage, SandeshLevel::UT_DEBUG,             \
                       IO_LOG_FLAG_SYSLOG, session, dir, arg)

#define TCP_UT_LOG_DEBUG(arg)                                                  \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    std::ostringstream out;                                                    \
    out << arg;                                                                \
    IO_LOG(TcpMessage, SandeshLevel::UT_DEBUG, IO_LOG_FLAG_SYSLOG, TCP_DIR_NA, \
           out.str());                                                         \
} while (false)

//
// Event Manager Log and Trace macros
//
#define EVENT_MANAGER_LOG_ERROR(arg)                                           \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    std::ostringstream out;                                                    \
    out << arg;                                                                \
    IO_LOG(EventManagerMessage, SandeshLevel::UT_ERR, IO_LOG_FLAG_ALL,         \
           out.str());                                                         \
} while (false)


#endif // __IO_LOG_H__

