/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_SERVICES_INIT__
#define __VNSW_SERVICES_INIT__

#include <sandesh/sandesh_trace.h>

class MetadataProxy;

class ServicesModule {
public:
    enum ServiceList {
        ArpService,
        DhcpService,
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

private:
    Agent *agent_;
    std::string metadata_secret_key_;
    boost::scoped_ptr<DhcpProto> dhcp_proto_;
    boost::scoped_ptr<DnsProto> dns_proto_;
    boost::scoped_ptr<ArpProto> arp_proto_;
    boost::scoped_ptr<IcmpProto> icmp_proto_;
    boost::scoped_ptr<MetadataProxy> metadata_proxy_;
};

extern SandeshTraceBufferPtr DhcpTraceBuf;
extern SandeshTraceBufferPtr ArpTraceBuf;
extern SandeshTraceBufferPtr MetadataTraceBuf;

#endif
