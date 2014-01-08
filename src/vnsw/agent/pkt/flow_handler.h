/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_handler_hpp
#define vnsw_agent_flow_handler_hpp

#include "pkt/proto_handler.h"

static const std::string unknown_vn_ = "__UNKNOWN__";

struct PktInfo;
class PktFlowInfo;

class FlowHandler : public ProtoHandler {
public:
    FlowHandler(Agent *agent, PktInfo *info, boost::asio::io_service &io) :
        ProtoHandler(agent, info, io) {}
    virtual ~FlowHandler() {}

    bool Run();
    void PktFlowTrace(const PktInfo *pkt_info,
                      const PktFlowInfo &flow_info);

    static const std::string *UnknownVn() { return &unknown_vn_; }
    static const std::string *LinkLocalVn() {
        return &Agent::GetInstance()->GetLinkLocalVnName();
    }

private:
};

#endif // vnsw_agent_flow_handler_hpp
