/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_PKT_INIT__
#define __VNSW_PKT_INIT__

#include <sandesh/sandesh_trace.h>

class PktModule {
public:
    PktModule(Agent *agent) : agent_(agent) { }
    ~PktModule() { }

    static void Init(bool run_with_vrouter);
    static void Shutdown();
    void CreateInterfaces();
private:
    Agent *agent_;
};

extern SandeshTraceBufferPtr PacketTraceBuf;

#endif // __VNSW_PKT_INIT__
