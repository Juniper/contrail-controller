/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifdef _WINDOWS
#include <boost/asio.hpp>
#include <windows.h>
 //This is required due to a dependency between boost and winsock that will result in:
 //fatal error C1189: #error :  WinSock.h has already been included
#endif
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

// Trace buffer for small messages
SandeshTraceBufferPtr IFMapTraceBuf(SandeshTraceBufferCreate("IFMapTraceBuf",
                                    1000));

// Trace buffer for update-sender messages
SandeshTraceBufferPtr IFMapUpdateSenderBuf(
    SandeshTraceBufferCreate("IFMapUpdateSenderBuf", 50000));

// Trace buffer for xmpp messages
SandeshTraceBufferPtr IFMapXmppTraceBuf(
    SandeshTraceBufferCreate("IFMapXmppTraceBuf", 50000));
