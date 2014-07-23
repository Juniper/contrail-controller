/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <services/icmp_error_proto.h>
#include <services/icmp_error_handler.h>

IcmpErrorProto::IcmpErrorProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::ICMP_ERROR, io) {
}

IcmpErrorProto::~IcmpErrorProto() {
}

ProtoHandler *IcmpErrorProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                                boost::asio::io_service &io) {
    return new IcmpErrorHandler(agent(), this, info, &io);
}

bool IcmpErrorProto::FlowIndexToKey(uint32_t index, FlowKey *key) {
    return flow_index_to_key_fn_(index, key);
}
