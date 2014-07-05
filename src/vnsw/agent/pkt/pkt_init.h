/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_PKT_INIT__
#define __VNSW_PKT_INIT__

#include <sandesh/sandesh_trace.h>

class PktHandler;
class FlowTable;
class FlowProto;

// Packet Module
class PktModule {
public:
    PktModule(Agent *agent);
    virtual ~PktModule();

    void Init(bool run_with_vrouter);
    void Shutdown();
    void IoShutdown();
    void FlushFlows();

    Agent *agent() const { return agent_; }
    PktHandler *pkt_handler() { return pkt_handler_.get(); }
    FlowTable *flow_table() { return flow_table_.get(); }

    void CreateInterfaces();

private:
    Agent *agent_;
    boost::scoped_ptr<PktHandler> pkt_handler_;
    boost::scoped_ptr<FlowTable> flow_table_;
    boost::scoped_ptr<FlowProto> flow_proto_;
    DISALLOW_COPY_AND_ASSIGN(PktModule);
};

extern SandeshTraceBufferPtr PacketTraceBuf;

#endif // __VNSW_PKT_INIT__
