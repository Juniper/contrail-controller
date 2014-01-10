/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_LOG_H__
#define __IFMAP_LOG_H__

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"

extern SandeshTraceBufferPtr IFMapTraceBuf;
extern SandeshTraceBufferPtr IFMapBigMsgTraceBuf;

// Log and trace regular messages

#define IFMAP_DEBUG_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define IFMAP_TRACE(obj, ...) \
do { \
    obj::TraceMsg(IFMapTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while(0)

#define IFMAP_DEBUG(obj, ...) \
do { \
    IFMAP_DEBUG_LOG(obj, Category::IFMAP, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_DEBUG_ONLY(obj, ...) \
do { \
    IFMAP_DEBUG_LOG(obj, Category::IFMAP, __VA_ARGS__); \
} while(0)

#define IFMAP_XMPP_DEBUG(obj, ...) \
do { \
    IFMAP_DEBUG_LOG(obj, Category::IFMAP_XMPP, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_PEER_DEBUG(obj, ...) \
do { \
    IFMAP_DEBUG_LOG(obj, Category::IFMAP_PEER, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_SM_DEBUG(obj, ...) \
do { \
    IFMAP_DEBUG_LOG(obj, Category::IFMAP_STATE_MACHINE, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

// Log and trace big-sized messages

#define IFMAP_DEBUG_LOG_BIG_MSG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define IFMAP_TRACE_BIG_MSG(obj, ...) \
do { \
    obj::TraceMsg(IFMapBigMsgTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while(0)

#define IFMAP_PEER_LOG_POLL_RESP(obj, ...) \
do { \
    IFMAP_DEBUG_LOG_BIG_MSG(obj, Category::IFMAP_PEER, __VA_ARGS__); \
    IFMAP_TRACE_BIG_MSG(obj##Trace, __VA_ARGS__); \
} while(0)

// Warnings

#define IFMAP_WARN_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_WARN, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define IFMAP_WARN(obj, ...) \
do { \
    IFMAP_WARN_LOG(obj, Category::IFMAP, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_XMPP_WARN(obj, ...) \
do { \
    IFMAP_WARN_LOG(obj, Category::IFMAP_XMPP, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_PEER_WARN(obj, ...) \
do { \
    IFMAP_WARN_LOG(obj, Category::IFMAP_PEER, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_SM_WARN(obj, ...) \
do { \
    IFMAP_WARN_LOG(obj, Category::IFMAP_STATE_MACHINE, __VA_ARGS__); \
    IFMAP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#endif // __IFMAP_LOG_H__
