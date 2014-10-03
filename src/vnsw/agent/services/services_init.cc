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
#include "services/dhcpv6_proto.h"
#include "services/dns_proto.h"
#include "services/arp_proto.h"
#include "services/icmp_proto.h"
#include "services/icmpv6_proto.h"
#include "services/metadata_proxy.h"
#include "init/agent_param.h"


SandeshTraceBufferPtr DhcpTraceBuf(SandeshTraceBufferCreate("Dhcp", 1000));
SandeshTraceBufferPtr Dhcpv6TraceBuf(SandeshTraceBufferCreate("Dhcpv6", 1000));
SandeshTraceBufferPtr Icmpv6TraceBuf(SandeshTraceBufferCreate("Icmpv6", 500));
SandeshTraceBufferPtr ArpTraceBuf(SandeshTraceBufferCreate("Arp", 1000));
SandeshTraceBufferPtr MetadataTraceBuf(SandeshTraceBufferCreate("Metadata", 500));

ServicesModule::ServicesModule(Agent *agent, const std::string &metadata_secret) 
    : agent_(agent), metadata_secret_key_(metadata_secret), dhcp_proto_(NULL),
      dhcpv6_proto_(NULL), dns_proto_(NULL), arp_proto_(NULL),
      icmp_proto_(NULL), icmpv6_proto_(NULL), metadata_proxy_(NULL) {
}

ServicesModule::~ServicesModule() {
}

void ServicesModule::Init(bool run_with_vrouter) {
    EventManager *event = agent_->event_manager();
    boost::asio::io_service &io = *event->io_service();

    dhcp_proto_.reset(new DhcpProto(agent_, io, run_with_vrouter));
    agent_->SetDhcpProto(dhcp_proto_.get());

    dhcpv6_proto_.reset(new Dhcpv6Proto(agent_, io, run_with_vrouter));
    agent_->set_dhcpv6_proto(dhcpv6_proto_.get());

    dns_proto_.reset(new DnsProto(agent_, io));
    agent_->SetDnsProto(dns_proto_.get());

    arp_proto_.reset(new ArpProto(agent_, io, run_with_vrouter));
    agent_->SetArpProto(arp_proto_.get());

    icmp_proto_.reset(new IcmpProto(agent_, io));
    agent_->SetIcmpProto(icmp_proto_.get());

    icmpv6_proto_.reset(new Icmpv6Proto(agent_, io));
    agent_->set_icmpv6_proto(icmpv6_proto_.get());

    icmp_error_proto_.reset(new IcmpErrorProto(agent_, io));

    metadata_proxy_.reset(new MetadataProxy(this, metadata_secret_key_));
}

void ServicesModule::ConfigInit() {
    dns_proto_->ConfigInit();
}

void ServicesModule::IoShutdown() {
    dns_proto_->IoShutdown();
    metadata_proxy_->CloseSessions();
}

void ServicesModule::Shutdown() {
    dhcp_proto_->Shutdown();
    dhcp_proto_.reset(NULL);
    agent_->SetDhcpProto(NULL);

    dhcpv6_proto_->Shutdown();
    dhcpv6_proto_.reset(NULL);
    agent_->set_dhcpv6_proto(NULL);

    dns_proto_->Shutdown();
    dns_proto_.reset(NULL);
    agent_->SetDnsProto(NULL);

    arp_proto_->Shutdown();
    arp_proto_.reset(NULL);
    agent_->SetArpProto(NULL);

    icmp_proto_->Shutdown();
    icmp_proto_.reset(NULL);
    agent_->SetIcmpProto(NULL);

    icmpv6_proto_->Shutdown();
    icmpv6_proto_.reset(NULL);
    agent_->set_icmpv6_proto(NULL);

    metadata_proxy_->Shutdown();
    metadata_proxy_.reset(NULL);
}
