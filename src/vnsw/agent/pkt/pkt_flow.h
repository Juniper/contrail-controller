/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_flow_hpp
#define vnsw_agent_pkt_flow_hpp

#include <net/if.h>
#include "cmn/agent_cmn.h"
#include "base/queue_task.h"
#include "pkt/proto.h"
#include "pkt/flowtable.h"

static const std::string unknown_vn_ = "__UNKNOWN__";

class FlowHandler : public ProtoHandler {
public:
    FlowHandler(PktInfo *info, boost::asio::io_service &io) :
        ProtoHandler(info, io) { };
    virtual ~FlowHandler() { };
    bool Run();
    void PktFlowTrace(const struct PktInfo *pkt_info, const PktFlowInfo &flow_info);
    static void Init(boost::asio::io_service &io);
    static void Shutdown();
    static const std::string *UnknownVn() {return &unknown_vn_;};
    static const std::string *LinkLocalVn() {return &Agent::GetLinkLocalVnName();};
private:
};

extern SandeshTraceBufferPtr PktFlowTraceBuf;

#define PKTFLOW_TRACE(obj, ...)\
do {\
    PktFlow##obj::TraceMsg(PktFlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif // vnsw_agent_pkt_flow_hpp
