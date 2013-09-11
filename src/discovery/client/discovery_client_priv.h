/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DISCOVERY_CLIENT_PRIV_H__
#define __DISCOVERY_CLIENT_PRIV_H__

extern SandeshTraceBufferPtr DiscoveryClientTraceBuf;

#define DISCOVERY_CLIENT_TRACE(obj, ...)\
do {\
    obj::TraceMsg(DiscoveryClientTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#endif // __DISCOVERY_CLIENT_PRIV_H__
