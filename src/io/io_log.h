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
#include "io/io_types.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"

#define IO_TRACE_BUF       "IOTraceBuf"
#define IO_LOG_FLAG_SYSLOG 1
#define IO_LOG_FLAG_TRACE  2
#define IO_LOG_FLAG_ALL    (IO_LOG_FLAG_SYSLOG | IO_LOG_FLAG_TRACE)

extern SandeshTraceBufferPtr IOTraceBuf;

#define IO_LOG(obj, level, flags, cat, ...)                                    \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    if ((flags) & IO_LOG_FLAG_SYSLOG) {                                        \
        obj##Log::Send(g_vns_constants.CategoryNames.find(cat)->second,        \
                       level, __FILE__, __LINE__, ##__VA_ARGS__);              \
    }                                                                          \
    if ((flags) & IO_LOG_FLAG_TRACE) {                                         \
        obj##Trace::TraceMsg(IOTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);   \
    }                                                                          \
} while (false)

//
// GENERIC Log and Trace macros
//
#define GEN_DIR_OUT ">"
#define GEN_DIR_IN  "<"
#define GEN_DIR_NA  ""

//
// TCP Log and Trace macros
//
#define TCP_DIR_OUT GEN_DIR_OUT
#define TCP_DIR_IN  GEN_DIR_IN
#define TCP_DIR_NA  GEN_DIR_NA

//
// UDP Log and Trace macros
//
#define UDP_DIR_OUT GEN_DIR_OUT
#define UDP_DIR_IN  GEN_DIR_IN
#define UDP_DIR_NA  GEN_DIR_NA

//
// Base macros to log and/or trace TCP messages with C++ string as last arg.
//
#define IO_SERVER_LOG_STR(msg, cat, obj, level, flags, server, dir, arg)       \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        if ((server) && (server)->DisableSandeshLogMessages()) {               \
            LOG(DEBUG, "Server " << (server)->ToString() << dir << " " << arg);\
            break;                                                             \
        }                                                                      \
        std::ostringstream out;                                                \
        out << arg;                                                            \
        IO_LOG(msg, level, flags, cat,                                         \
               (server) ? (server)->ToString() : "", dir, out.str());          \
    } while (false)

#define IO_SESSION_LOG_STR(msg, cat, obj, level, flags, session, dir, arg)    \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        if ((session) && (session)->server() &&                                \
            (session)->server()->DisableSandeshLogMessages()) {                \
            LOG(DEBUG, "Session " << (session)->ToString() << dir<< " "<< arg);\
            break;                                                             \
        }                                                                      \
        std::ostringstream out;                                                \
        out << arg;                                                            \
        IO_LOG(msg, level, flags, cat,                                         \
               (session) ? (session)->ToString() : "", dir, out.str());        \
    } while (false)

#define IO_UT_LOG_DEBUG(msg, cat, dir, arg)                                   \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    std::ostringstream out;                                                    \
    out << arg;                                                                \
    IO_LOG(msg, SandeshLevel::UT_DEBUG, IO_LOG_FLAG_SYSLOG, cat, dir,          \
           out.str());                                                         \
} while (false)

#define TCP_SERVER_LOG_STR(obj, level, flags, server, dir, arg)                \
     IO_SERVER_LOG_STR(TcpServerMessage, Category::TCP, obj, level, flags,     \
                       server, dir, arg)

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

#define TCP_SESSION_LOG_STR(obj, level, flags, session, dir, arg)              \
     IO_SESSION_LOG_STR(TcpSessionMessage, Category::TCP, obj, level, flags,   \
                        session, dir, arg)

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
     IO_UT_LOG_DEBUG(TcpMessage, Category::TCP, TCP_DIR_NA, arg)

//
// Event Manager Log and Trace macros
//
#define EVENT_MANAGER_LOG_ERROR(arg)                                           \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    std::ostringstream out;                                                    \
    out << arg;                                                                \
    IO_LOG(EventManagerMessage, SandeshLevel::UT_ERR, IO_LOG_FLAG_ALL,         \
           Category::TCP, out.str());                                          \
} while (false)

//
// Event Manager Log and Trace macros for UDP
//
#define UDP_EVENT_MANAGER_LOG_ERROR(arg)                                       \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    std::ostringstream out;                                                    \
    out << arg;                                                                \
    IO_LOG(EventManagerMessage, SandeshLevel::UT_ERR, IO_LOG_FLAG_ALL,         \
           Category::UDP, out.str());                                          \
} while (false)

#define UDP_SERVER_LOG_STR(obj, level, flags, server, dir, arg)                \
     IO_SERVER_LOG_STR(UdpServerMessage, Category::UDP, obj, level, flags,     \
                       server, dir, arg)
#define UDP_SERVER_LOG_ERROR(server, dir, arg)                                 \
    UDP_SERVER_LOG_STR(UdpServerMessage, SandeshLevel::SYS_ERR,                \
                       IO_LOG_FLAG_ALL, server, dir, arg)
#define UDP_SERVER_LOG_INFO(server, dir, arg)                                  \
    UDP_SERVER_LOG_STR(UdpServerMessage, SandeshLevel::SYS_INFO,               \
                       IO_LOG_FLAG_SYSLOG, server, dir, arg)
#define UDP_SERVER_LOG_DEBUG(server, dir, arg)                                 \
    UDP_SERVER_LOG_STR(UdpServerMessage, SandeshLevel::SYS_DEBUG,              \
                       IO_LOG_FLAG_SYSLOG, server, dir, arg)
#define UDP_SERVER_LOG_UT_DEBUG(server, dir, arg)                              \
    UDP_SERVER_LOG_STR(UdpServerMessage, SandeshLevel::UT_DEBUG,               \
                       IO_LOG_FLAG_SYSLOG, server, dir, arg)


#define UDP_SESSION_LOG_STR(obj, level, flags, session, dir, arg)              \
     IO_SESSION_LOG_STR(UdpSessionMessage, Category::UDP, obj, level, flags,   \
                        session, dir, arg)

#define UDP_UT_LOG_DEBUG(arg)                                                  \
     IO_UT_LOG_DEBUG(UdpMessage, Category::UDP, UDP_DIR_NA, arg)

#endif // __IO_LOG_H__

