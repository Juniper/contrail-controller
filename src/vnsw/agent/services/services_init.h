/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_SERVICES_INIT__
#define __VNSW_SERVICES_INIT__

#include <sandesh/sandesh_trace.h>

class ServicesModule {
    enum ServiceList {
        ArpService,
        DhcpService,
        DnsService,
    };

public:
    static void Init(bool run_with_vrouter);
    static void ConfigInit();
    static void Shutdown();
};

extern SandeshTraceBufferPtr DhcpTraceBuf;
extern SandeshTraceBufferPtr ArpTraceBuf;

#endif
