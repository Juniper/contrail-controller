/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <services/icmpv6_error_proto.h>
#include <services/icmpv6_error_handler.h>

Icmpv6ErrorProto::Icmpv6ErrorProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::ICMPV6_ERROR, io) {
    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);
}

Icmpv6ErrorProto::~Icmpv6ErrorProto() {
}

ProtoHandler *Icmpv6ErrorProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                                boost::asio::io_service &io) {
    return new Icmpv6ErrorHandler(agent(), this, info, &io);
}

