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
                boost::asio::io_service &io, FlowProto *proto, uint16_t index) :
                ProtoHandler(agent, info, io), flow_proto_(proto),
                flow_table_index_(index) {
    }
    virtual ~FlowHandler() {}

    bool Run();

    static const std::string UnknownVn() { return unknown_vn_; }
    static const VnListType UnknownVnList() {
        VnListType unknown_vn_list;
        unknown_vn_list.insert(unknown_vn_);
        return unknown_vn_list;
    }
    static const std::string LinkLocalVn() {
        return Agent::GetInstance()->linklocal_vn_name();
    }
    bool IsL3ModeFlow() const;

private:
    FlowProto *flow_proto_;
    uint16_t flow_table_index_;
};

#endif // vnsw_agent_flow_handler_hpp
