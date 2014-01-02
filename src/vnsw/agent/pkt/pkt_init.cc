/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_init.h"
#include "pkt/pkt_handler.h"
#include "pkt/proto.h"
#include "pkt/pkt_flow.h"
#include "pkt/flowtable.h"

SandeshTraceBufferPtr PacketTraceBuf(SandeshTraceBufferCreate("Packet", 1000));

void PktModule::Init(bool run_with_vrouter) {
    EventManager *event = Agent::GetInstance()->GetEventManager();
    boost::asio::io_service &io = *event->io_service();
    std::string ifname(Agent::GetInstance()->pkt_interface_name());

    PktHandler::Init(Agent::GetInstance()->GetDB(), ifname, io, run_with_vrouter);
    FlowTable::Init();
    FlowHandler::Init(io);
}

void PktModule::Shutdown() {
    FlowHandler::Shutdown();
    PktHandler::Shutdown();
    FlowTable::Shutdown();
}

void PktModule::CreateInterfaces() {
    std::string ifname(Agent::GetInstance()->pkt_interface_name());
    PktHandler::CreateHostInterface(ifname);
}
