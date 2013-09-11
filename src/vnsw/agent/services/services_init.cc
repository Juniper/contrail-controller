/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/mirror_table.h>
#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "services/services_init.h"
#include "services/dhcp_proto.h"
#include "services/dns_proto.h"
#include "services/arp_proto.h"
#include "services/icmp_proto.h"


SandeshTraceBufferPtr DhcpTraceBuf(SandeshTraceBufferCreate("Dhcp", 1000));
SandeshTraceBufferPtr ArpTraceBuf(SandeshTraceBufferCreate("Arp", 1000));

void ServicesModule::Init(bool run_with_vrouter) {
    EventManager *event = Agent::GetInstance()->GetEventManager();
    boost::asio::io_service &io = *event->io_service();

    DhcpProto::Init(io, run_with_vrouter);
    DnsProto::Init(io);
    ArpProto::Init(io, run_with_vrouter);
    IcmpProto::Init(io);
}

void ServicesModule::ConfigInit() {
    DnsProto::ConfigInit();
}

void ServicesModule::Shutdown() {
    DhcpProto::Shutdown();
    DnsProto::Shutdown();
    ArpProto::Shutdown();
    IcmpProto::Shutdown();
}
