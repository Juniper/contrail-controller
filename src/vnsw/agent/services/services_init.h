/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_SERVICES_INIT__
#define __VNSW_SERVICES_INIT__

#include <sandesh/sandesh_trace.h>
#include <pkt/proto.h>
#include <services/icmp_error_proto.h>
#include <services/icmpv6_error_proto.h>
#include <services/arp_proto.h>

class MetadataProxy;

class ServicesModule {
public:
    enum ServiceList {
        ArpService,
        DhcpService,
        Dhcpv6Service,
        DnsService,
    };
    ServicesModule(Agent *agent, const std::string &metadata_secret);
    ~ServicesModule();

    Agent *agent() { return agent_; }
    MetadataProxy *metadataproxy() { return metadata_proxy_.get(); }

    void Init(bool run_with_vrouter);
    void ConfigInit();
    void Shutdown();
    void IoShutdown();
    IcmpErrorProto *icmp_error_proto() const {
        return icmp_error_proto_.get();
    }

    ArpProto *arp_proto() const {
        return arp_proto_.get();
    }

private:
    Agent *agent_;
    std::string metadata_secret_key_;
    boost::scoped_ptr<DhcpProto> dhcp_proto_;
    boost::scoped_ptr<Dhcpv6Proto> dhcpv6_proto_;
    boost::scoped_ptr<DnsProto> dns_proto_;
    boost::scoped_ptr<ArpProto> arp_proto_;
    boost::scoped_ptr<BfdProto> bfd_proto_;
    boost::scoped_ptr<IcmpProto> icmp_proto_;
    boost::scoped_ptr<Icmpv6Proto> icmpv6_proto_;
    boost::scoped_ptr<IcmpErrorProto> icmp_error_proto_;
    boost::scoped_ptr<Icmpv6ErrorProto> icmpv6_error_proto_;
    boost::scoped_ptr<MetadataProxy> metadata_proxy_;
};

extern SandeshTraceBufferPtr DhcpTraceBuf;
extern SandeshTraceBufferPtr Dhcpv6TraceBuf;
extern SandeshTraceBufferPtr Icmpv6TraceBuf;
extern SandeshTraceBufferPtr ArpTraceBuf;
extern SandeshTraceBufferPtr MetadataTraceBuf;
extern SandeshTraceBufferPtr BfdTraceBuf;

#endif
