/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_init.h"
#include "pkt/pkt_handler.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_table.h"

SandeshTraceBufferPtr PacketTraceBuf(SandeshTraceBufferCreate("Packet", 1000));

PktModule::PktModule(Agent *agent) : agent_(agent), pkt_handler_(NULL) {
}

PktModule::~PktModule() {
}

void PktModule::Init(bool run_with_vrouter) {
    EventManager *event = agent_->GetEventManager();
    boost::asio::io_service &io = *event->io_service();
    std::string ifname(agent_->pkt_interface_name());

    pkt_handler_.reset(new PktHandler(agent_->GetDB(), ifname,
                                      io, run_with_vrouter));
    pkt_handler_->Init();

    flow_table_.reset(new FlowTable());
    flow_table_->Init();

    flow_proto_.reset(new FlowProto(io));
    flow_proto_->Init();
}

void PktModule::Shutdown() {
    flow_proto_->Shutdown();
    flow_proto_.reset(NULL);

    pkt_handler_->Shutdown();
    pkt_handler_.reset(NULL);

    flow_table_->Shutdown();
    flow_table_.reset(NULL);
}

void PktModule::CreateInterfaces() {
<<<<<<< HEAD
    std::string ifname(agent_->GetHostIfname());
    pkt_handler_->CreateHostInterface(ifname);
=======
    std::string ifname(Agent::GetInstance()->pkt_interface_name());
    PktHandler::CreateHostInterface(ifname);
>>>>>>> upstream/master
}
