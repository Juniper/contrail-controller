/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONFIG_CLIENT_LOG_H__
#define __CONFIG_CLIENT_LOG_H__

#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"

extern SandeshTraceBufferPtr ConfigClientTraceBuf;
extern SandeshTraceBufferPtr ConfigClientBigMsgTraceBuf;
extern SandeshTraceBufferPtr ConfigCassClientTraceBuf;

// Log and trace regular messages

#define CONFIG_CLIENT_DEBUG_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(ConfigClientTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_DEBUG(obj, ...) \
do { \
    CONFIG_CLIENT_DEBUG_LOG(obj, Category::CONFIG_CLIENT, __VA_ARGS__); \
    CONFIG_CLIENT_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)

#define CONFIG_CLIENT_DEBUG_ONLY(obj, ...) \
do { \
    CONFIG_CLIENT_DEBUG_LOG(obj, Category::CONFIG_CLIENT, __VA_ARGS__); \
} while(0)

#define CONFIG_CASS_CLIENT_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(ConfigCassClientTraceBuf, __FILE__, __LINE__, \
                      __VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CASS_CLIENT_DEBUG(obj, ...) \
do { \
    CONFIG_CLIENT_DEBUG_LOG(obj, Category::CONFIG_CASS_CLIENT, __VA_ARGS__); \
    CONFIG_CASS_CLIENT_TRACE(obj##Trace, __VA_ARGS__); \
} while(0)
// Log and trace big-sized messages

#define CONFIG_CLIENT_DEBUG_LOG_BIG_MSG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define CONFIG_CLIENT_TRACE_BIG_MSG(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(ConfigClientMapBigMsgTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
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

#endif // __CONFIG_CLIENT_LOG_H__
