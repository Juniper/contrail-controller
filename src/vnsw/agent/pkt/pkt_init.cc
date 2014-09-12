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
#include "pkt/packet_buffer.h"
#include "pkt/control_interface.h"

SandeshTraceBufferPtr PacketTraceBuf(SandeshTraceBufferCreate("Packet", 1000));

PktModule::PktModule(Agent *agent) 
    : agent_(agent), control_interface_(NULL), pkt_handler_(NULL),
    flow_table_(NULL), flow_proto_(NULL),
    packet_buffer_manager_(new PacketBufferManager(this)) {
}

PktModule::~PktModule() {
}

void PktModule::Init(bool run_with_vrouter) {
    boost::asio::io_service &io = *agent_->event_manager()->io_service();

    pkt_handler_.reset(new PktHandler(agent_, this));

    if (control_interface_) {
        control_interface_->Init(pkt_handler_.get());
    }

    flow_table_.reset(new FlowTable(agent_));
    flow_table_->Init();

    flow_proto_.reset(new FlowProto(agent_, io));
    flow_proto_->Init();
}

void PktModule::InitDone() {
    flow_table_->InitDone();
}

void PktModule::set_control_interface(ControlInterface *intf) {
    control_interface_ = intf;
}

void PktModule::Shutdown() {
    flow_proto_->Shutdown();
    flow_proto_.reset(NULL);

    flow_table_->Shutdown();
    flow_table_.reset(NULL);

    control_interface_->Shutdown();
    control_interface_ = NULL;
}

void PktModule::IoShutdown() {
    control_interface_->IoShutdown();
}

void PktModule::FlushFlows() {
    flow_table_->DeleteAll();
}

void PktModule::CreateInterfaces() {
    if (control_interface_ == NULL)
        return;

    PacketInterface::Create(agent_->interface_table(),
                            control_interface_->Name());
}
