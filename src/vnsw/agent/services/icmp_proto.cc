/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <services/icmp_proto.h>

IcmpProto::IcmpProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::ICMP, io) {
}

IcmpProto::~IcmpProto() {
}

ProtoHandler *IcmpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new IcmpHandler(agent(), info, io);
}
