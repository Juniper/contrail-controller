/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

// Trace buffer for config-client-manager messages
SandeshTraceBufferPtr ConfigClientTraceBuf(
    SandeshTraceBufferCreate("ConfigClientTraceBuf", 50000));

// Trace buffer for rabbit messages
SandeshTraceBufferPtr ConfigClientRabbitMsgTraceBuf(
    SandeshTraceBufferCreate("ConfigClientRabbitMsgTraceBuf", 50000));
