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
extern SandeshTraceBufferPtr IFMapUpdateSenderBuf;
extern SandeshTraceBufferPtr IFMapXmppTraceBuf;
extern SandeshTraceBufferPtr ConfigClientTraceBuf;
extern SandeshTraceBufferPtr ConfigClientRabbitMsgTraceBuf;

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
    if (!LoggingDisabled()) { \
        obj::TraceMsg(IFMapTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
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

#define IFMAP_XMPP_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(IFMapXmppTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while(0)

#define IFMAP_XMPP_DEBUG(obj, ...) \
do { \
    IFMAP_DEBUG_LOG(obj, Category::IFMAP_XMPP, __VA_ARGS__); \
    IFMAP_XMPP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define IFMAP_UPD_SENDER_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(IFMapUpdateSenderBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_RABBIT_MSG_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(ConfigClientRabbitMsgTraceBuf, __FILE__, __LINE__, \
        __VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(ConfigClientTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_DEBUG_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_DEBUG(obj, ...) \
do { \
    CONFIG_CLIENT_DEBUG_LOG(obj, Category::CONFIG_CLIENT, __VA_ARGS__); \
    CONFIG_CLIENT_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

// Warnings

#define CONFIG_CLIENT_WARN_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_WARN, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_WARN(obj, ...) \
do { \
    CONFIG_CLIENT_WARN_LOG(obj, Category::CONFIG_CLIENT, __VA_ARGS__); \
    CONFIG_CLIENT_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

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
    IFMAP_XMPP_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#endif // __IFMAP_LOG_H__
