/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <services/icmp_error_proto.h>
#include <services/icmp_error_handler.h>

IcmpErrorProto::IcmpErrorProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::ICMP_ERROR, io) {
    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);
}

IcmpErrorProto::~IcmpErrorProto() {
}

ProtoHandler *IcmpErrorProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                                boost::asio::io_service &io) {
    return new IcmpErrorHandler(agent(), this, info, &io);
}

bool IcmpErrorProto::FlowIndexToKey(uint32_t index, FlowKey *key) {
    if (flow_index_to_key_fn_.empty())
        return false;
    return flow_index_to_key_fn_(index, key);
}
