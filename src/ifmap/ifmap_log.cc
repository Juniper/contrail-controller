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

// Trace buffer for peer messages
SandeshTraceBufferPtr IFMapPeerTraceBuf(
    SandeshTraceBufferCreate("IFMapPeerTraceBuf", 50000));

// Trace buffer for state-machine messages
SandeshTraceBufferPtr IFMapSmTraceBuf(
    SandeshTraceBufferCreate("IFMapSmTraceBuf", 50000));

// Trace buffer for update-sender messages
SandeshTraceBufferPtr IFMapUpdateSenderBuf(
    SandeshTraceBufferCreate("IFMapUpdateSenderBuf", 50000));

// Trace buffer for xmpp messages
SandeshTraceBufferPtr IFMapXmppTraceBuf(
    SandeshTraceBufferCreate("IFMapXmppTraceBuf", 50000));

