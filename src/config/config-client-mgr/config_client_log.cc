/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

// Trace buffer for small messages
SandeshTraceBufferPtr ConfigClientTraceBuf(
          SandeshTraceBufferCreate("ConfigClientTraceBuf", 1000));

// Trace buffer for large messages like poll responses
SandeshTraceBufferPtr ConfigClientBigMsgTraceBuf(
    SandeshTraceBufferCreate("ConfigClientBigMsgTraceBuf", 25));

// Trace buffer for config-cassandra-client messages
SandeshTraceBufferPtr ConfigCassClientTraceBuf(
    SandeshTraceBufferCreate("ConfigCassClientTraceBuf", 50000));
