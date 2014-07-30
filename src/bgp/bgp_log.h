/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_log_h

#define ctrlplane_bgp_log_h

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "base/logging.h"
#include "bgp/bgp_log_types.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_table.h"

namespace bgp_log_test {

void init();
void init(std::string log_file, unsigned long log_file_size,
          unsigned long log_file_index, bool enable_syslog,
          std::string syslog_facility, std::string ident);
bool unit_test();
void LogServerName(const BgpServer *server);
void LogServerName(const IPeer *ipeer, const BgpTable *table);

// Bgp Unit Test specific logging macros
#define BGP_DEBUG_UT(str) \
    BGP_LOG_STR(BgpMessage, Sandesh::LoggingUtLevel(), BGP_LOG_FLAG_ALL, str)
#define BGP_WARN_UT(str) \
    BGP_LOG_STR(BgpMessage, SandeshLevel::UT_WARN, BGP_LOG_FLAG_ALL, str)

}

#define BGP_TRACE_BUF             "BgpTraceBuf"
#define BGP_PEER_OBJECT_TRACE_BUF "BgpPeerObjectTraceBuf"

extern SandeshTraceBufferPtr BgpTraceBuf;
extern SandeshTraceBufferPtr BgpPeerObjectTraceBuf;

#define BGP_LOG_FLAG_SYSLOG 1
#define BGP_LOG_FLAG_TRACE  2
#define BGP_LOG_FLAG_ALL  (BGP_LOG_FLAG_SYSLOG | BGP_LOG_FLAG_TRACE)

// Base macro to log and/or trace BGP messages
#define BGP_LOG(obj, level, flags, ...)                                    \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    if ((flags) & BGP_LOG_FLAG_SYSLOG) {                                   \
        obj##Log::Send(g_vns_constants.CategoryNames.find(                 \
                       Category::BGP)->second, level,                      \
                       __FILE__, __LINE__, ##__VA_ARGS__);                 \
        if (bgp_log_test::unit_test()) break;                              \
    }                                                                      \
    if ((flags) & BGP_LOG_FLAG_TRACE) {                                    \
        const std::string __trace_buf(BGP_TRACE_BUF);                      \
        obj##Trace::TraceMsg(BgpTraceBuf, __FILE__, __LINE__,              \
                             ##__VA_ARGS__);                               \
    }                                                                      \
} while (false)

// Base macro to log and/or trace BGP messages with C++ string as last arg
#define BGP_LOG_STR(obj, level, flags, arg)                                \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    std::ostringstream _os;                                                \
    _os << arg;                                                            \
    BGP_LOG(obj, level, flags, _os.str());                                 \
} while (false)

// Log BgpServer information if available
//
// XXX Only used in unit tests. In production, there is only one BgpServer per
// control-node daemon
#define BGP_LOG_SERVER(peer, table)                                        \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    bgp_log_test::LogServerName(dynamic_cast<const IPeer *>(peer),         \
                                dynamic_cast<const BgpTable *>(table));    \
} while (false)

// BgpPeer specific logging macros
#define BGP_PEER_DIR_OUT "SEND"
#define BGP_PEER_DIR_IN  "RECV"
#define BGP_PEER_DIR_NA  ""

#define BGP_LOG_PEER_INTERNAL(type, peer, level, flags, ...)               \
do {                                                                       \
    IPeer *_peer = dynamic_cast<IPeer *>(peer);                            \
    std::string _peer_name;                                                \
    if (_peer)  {                                                          \
        _peer_name = _peer->ToUVEKey();                                    \
    } else {                                                               \
        _peer_name = "Unknown";                                            \
    }                                                                      \
    if (_peer && _peer->IsXmppPeer()) {                                    \
        BGP_LOG(XmppPeer ## type, level, flags, _peer_name, ##__VA_ARGS__);\
    } else {                                                               \
        BGP_LOG(BgpPeer ## type, level, flags, _peer_name, ##__VA_ARGS__); \
    }                                                                      \
} while (false)

// Grab all the macro arguments
#define BGP_LOG_PEER(type, peer, level, flags, dir, arg)                   \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
                                                                           \
    BGP_LOG_SERVER(peer, (BgpTable *) 0);                                  \
    std::ostringstream _os;                                                \
    _os << arg;                                                            \
    BGP_LOG_PEER_INTERNAL(type, peer, level, flags, dir, _os.str());       \
} while (false)

#define BGP_LOG_PEER_TABLE(peer, level, flags, tbl, arg)                   \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    BGP_LOG_SERVER(peer, tbl);                                             \
    std::ostringstream _os;                                                \
    _os << arg;                                                            \
    BGP_LOG_PEER_INTERNAL(Table, peer, level, flags, BGP_PEER_DIR_NA,      \
            (tbl) ? (tbl)->name() : "",                                    \
            ((tbl) && (tbl)->routing_instance()) ?                         \
                (tbl)->routing_instance()->name() : "",                    \
            _os.str());                                                    \
} while (false);

#define BGP_LOG_PEER_INSTANCE(peer, instance, level, flags, arg)           \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    std::ostringstream _os;                                                \
    _os << arg;                                                            \
    BGP_LOG_PEER_INTERNAL(Instance, peer, level, flags, BGP_PEER_DIR_NA,   \
                          instance, _os.str());                            \
} while (false)

#define BGP_LOG_SCHEDULING_GROUP(peer, arg)                                \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    BGP_LOG_SERVER(peer, (BgpTable *) 0);                                  \
    ostringstream _os;                                                     \
    _os << arg;                                                            \
    BGP_LOG_PEER_INTERNAL(SchedulingGroup, peer, SandeshLevel::SYS_DEBUG,  \
                          BGP_LOG_FLAG_TRACE, BGP_PEER_DIR_NA, _os.str()); \
} while (false)

// Bgp Route specific logging macro
#define BGP_LOG_ROUTE(table, peer, route, arg)                             \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    BGP_LOG_SERVER(peer, table);                                           \
    ostringstream _os;                                                     \
    _os << arg;                                                            \
    BGP_LOG_PEER_INTERNAL(Route, peer, SandeshLevel::SYS_DEBUG,            \
            BGP_LOG_FLAG_TRACE, BGP_PEER_DIR_NA, _os.str(),                \
            (route) ? (route)->ToString() : "",                            \
            (table) ? (table)->name() : "");                               \
} while (false)

// Bgp Table specific logging macro
#define BGP_LOG_TABLE(table, level, flags, arg)                            \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    BGP_LOG_SERVER((IPeer *) 0, table);                                    \
    ostringstream _os;                                                     \
    _os << arg;                                                            \
    BGP_LOG(BgpTable, level, flags, (table) ? (table)->name() : "",        \
            _os.str());                                                    \
} while (false)

#define BGP_CONFIG_LOG_INTERNAL(type, server, level, flags, ...)               \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    if ((flags) & BGP_LOG_FLAG_SYSLOG) {                                       \
        bgp_log_test::LogServerName(server);                                   \
        BgpConfig##type##Log::Send(g_vns_constants.CategoryNames.find(         \
                                    Category::BGP_CONFIG)->second,             \
                                    level, __FILE__, __LINE__, ##__VA_ARGS__); \
    }                                                                          \
    if ((flags) & BGP_LOG_FLAG_TRACE) {                                        \
        const std::string __trace_buf(BGP_TRACE_BUF);                          \
        BgpConfig##type##Trace::TraceMsg(BgpTraceBuf,                          \
                                         __FILE__, __LINE__, ##__VA_ARGS__);   \
    }                                                                          \
} while (false)

#define BGP_CONFIG_LOG_INSTANCE(type, server, rtinstance, level, flags, ...)   \
    BGP_CONFIG_LOG_INTERNAL(Instance##type, server, level, flags,              \
                            (rtinstance)->name(), ##__VA_ARGS__);

#define BGP_CONFIG_LOG_NEIGHBOR(type, server, neighbor, level, flags, ...)     \
    BGP_CONFIG_LOG_INTERNAL(Neighbor##type, server, level, flags,              \
                            (neighbor)->name(), ##__VA_ARGS__);

#define BGP_CONFIG_LOG_PEERING(type, server, peering, level, flags, ...)       \
    BGP_CONFIG_LOG_INTERNAL(Peering##type, server, level, flags,               \
                            (peering)->name(), ##__VA_ARGS__);

#define BGP_CONFIG_LOG_PROTOCOL(type, server, protocol, level, flags, ...)     \
    BGP_CONFIG_LOG_INTERNAL(Protocol##type, server, level, flags,              \
                            (protocol)->instance()->name(), ##__VA_ARGS__);

// BGP Trace macros.

#define BGP_TRACE_PEER_OBJECT(peer, peer_info, level)                      \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    BGP_LOG_SERVER(peer, (BgpTable *) 0);                                  \
    BgpPeerObjectTrace::TraceMsg(BgpPeerObjectTraceBuf, __FILE__, __LINE__,\
                                 peer_info);                               \
} while (false)

#define BGP_TRACE_PEER_PACKET(peer, msg, size, level)                      \
do {                                                                       \
    if (LoggingDisabled()) break;                                          \
    if ((level) > Sandesh::LoggingLevel()) break;                          \
    BgpPeerInfo peer_info;                                                 \
                                                                           \
    BGP_LOG_SERVER(peer, (BgpTable *) 0);                                  \
    (peer)->state_machine()->SetDataCollectionKey(&peer_info);             \
    peer_info.set_packet_data((peer)->BytesToHexString(msg, size));        \
    BGP_TRACE_PEER_OBJECT(peer, peer_info, level);                         \
} while (false)

#endif // ctrlplane_bgp_log_h
