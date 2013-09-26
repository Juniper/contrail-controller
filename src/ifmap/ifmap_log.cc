/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

// Trace buffer for small messages
SandeshTraceBufferPtr IFMapTraceBuf(SandeshTraceBufferCreate("IFMapTraceBuf",
                                    1000));

// Trace buffer for large messages like poll responses
SandeshTraceBufferPtr IFMapBigMsgTraceBuf(
    SandeshTraceBufferCreate("IFMapBigMsgTraceBuf", 25));

