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
    FlowProto(boost::asio::io_service &io) :
        Proto("Agent::FlowHandler", PktHandler::FLOW, io) {
        Agent::GetInstance()->SetFlowProto(this);
    }
    virtual ~FlowProto() {}
    void Init() {}
    void Shutdown() {}

    FlowHandler *AllocProtoHandler(PktInfo *info, boost::asio::io_service &io) {
        return new FlowHandler(info, io);
    }

    bool Validate(PktInfo *msg) {
        if (msg->ip == NULL) {
            FLOW_TRACE(DetailErr, msg->agent_hdr.cmd_param,
                       msg->agent_hdr.ifindex, msg->agent_hdr.vrf,
                       msg->ip_saddr, msg->ip_daddr,
                       "Flow : Non-IP packet. Dropping");
            return false;
        }
        return true;
    }

    bool RemovePktBuff() {
        return true;
    }
};

extern SandeshTraceBufferPtr PktFlowTraceBuf;

#define PKTFLOW_TRACE(obj, ...)\
do {\
    PktFlow##obj::TraceMsg(PktFlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif // vnsw_agent_flow_proto_hpp
