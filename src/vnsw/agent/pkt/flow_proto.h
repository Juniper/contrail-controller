/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_proto_hpp
#define vnsw_agent_flow_proto_hpp

#include <net/if.h>
#include "cmn/agent_cmn.h"
#include "base/queue_task.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/flow_table.h"
#include "pkt/flow_handler.h"

class FlowProto : public Proto {
public:
    static const std::string kFlowTaskName;
    static const int kIterations = 128;
    FlowProto(Agent *agent, boost::asio::io_service &io) :
        Proto(agent, kFlowHandlerTask.c_str(), PktHandler::FLOW, io,
              kIterations) {
        agent->SetFlowProto(this);
        set_trace(false);
    }
    virtual ~FlowProto() {}
    void Init() {}
    void Shutdown() {}

    FlowHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                   boost::asio::io_service &io) {
        return new FlowHandler(agent(), info, io);
    }
};

extern SandeshTraceBufferPtr PktFlowTraceBuf;

#define PKTFLOW_TRACE(obj, ...)\
do {\
    PktFlow##obj::TraceMsg(PktFlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif // vnsw_agent_flow_proto_hpp
