/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_LOG_H__
#define __XMPP_LOG_H__

#include "base/logging.h"
#include "sandesh/sandesh_trace.h"

#define XMPP_MESSAGE_TRACE_BUF "XmppMessageTrace"
#define XMPP_TRACE_BUF "XmppTrace"

#define XMPP_LOG XMPP_INFO
#define XMPP_ERROR(obj, ...)                                                   \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);       \
} while (false)
extern SandeshTraceBufferPtr XmppMessageTraceBuf;
extern SandeshTraceBufferPtr XmppTraceBuf;

#define XMPP_ALERT(obj, ...)                                                   \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              SandeshLevel::SYS_ALERT, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)

#define XMPP_WARNING(obj, ...)                                                 \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              SandeshLevel::SYS_WARN, __FILE__, __LINE__, ##__VA_ARGS__);      \
} while (false)

#define XMPP_NOTICE(obj, ...)                                                  \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              SandeshLevel::SYS_NOTICE, __FILE__, __LINE__, ##__VA_ARGS__);    \
} while (false)

#define XMPP_INFO(obj, ...)                                                    \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              SandeshLevel::SYS_INFO, __FILE__, __LINE__, ##__VA_ARGS__);      \
} while (false)

#define XMPP_DEBUG(obj, ...)                                                   \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)

#define XMPP_UTDEBUG(obj, ...)                                                 \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    obj::Send(g_vns_constants.CategoryNames.find(Category::XMPP)->second,      \
              Sandesh::LoggingUtLevel(), __FILE__, __LINE__, ##__VA_ARGS__);      \
} while (false)

#define XMPP_TRACE(obj, ...) do {                                              \
    obj::TraceMsg(XmppTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);            \
} while (0);

#define XMPP_MESSAGE_TRACE(obj, ...) do {                                      \
    if (LoggingDisabled()) break;                                              \
    obj::TraceMsg(XmppMessageTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (0);

#define XMPP_CONNECTION_LOG_MSG(info)                                          \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    XMPP_CONNECTION_LOG_SEND(info);                                            \
} while (false)

#endif // __XMPP_LOG_H__
