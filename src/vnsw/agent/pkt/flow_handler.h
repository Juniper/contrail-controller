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
    FlowHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                boost::asio::io_service &io)
              : ProtoHandler(agent, info, io) {}
    virtual ~FlowHandler() {}

    bool Run();

    static const std::string *UnknownVn() { return &unknown_vn_; }
    static const std::string *LinkLocalVn() {
        return &Agent::GetInstance()->linklocal_vn_name();
    }

private:
};

#endif // vnsw_agent_flow_handler_hpp
