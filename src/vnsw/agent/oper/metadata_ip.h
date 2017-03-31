/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OPER_METADATA_IP_H_
#define SRC_VNSW_AGENT_OPER_METADATA_IP_H_

#include <cmn/agent.h>
#include <cmn/index_vector.h>

class Agent;
class MetaDataIpAllocator;

///////////////////////////////////////////////////////////////////////////////
// MetaDataIp module handles creation/management of LinkLocal/Metadata IP
// used to provide underlay to overlay access on a particular
// Virtual-Machine-Interface.
//
// MetaDataIP installs metadata IP route in fabric VRF with rules to set
// source and destination Address to be used to do NAT, which can be used
// to associate single interface with multiple metadata IPs defining separate
// NAT source/destination IP.
//
// MetaDataIP becomes active only when interface is L3 Active and have a
// valid mpls label associated, which needs to be used to export linklocal
// route
///////////////////////////////////////////////////////////////////////////////

class MetaDataIp {
public:
    static const IpAddress kDefaultIp;

    enum MetaDataIpType {
        LINKLOCAL = 0,
        HEALTH_CHECK,
        INVALID
    };

    MetaDataIp(MetaDataIpAllocator *allocator, VmInterface *intf,
               MetaDataIpType type);
    MetaDataIp(MetaDataIpAllocator *allocator, VmInterface *intf,
               uint16_t index);
    ~MetaDataIp();

    Ip4Address GetLinkLocalIp() const;

    IpAddress service_ip() const;

    IpAddress destination_ip() const;
    void set_destination_ip(const IpAddress &dst_ip);

    void set_active(bool active);

    void UpdateInterfaceCb();

private:
    friend class MetaDataIpAllocator;

    void UpdateRoute();

    MetaDataIpAllocator *allocator_;
    // index represents the metadata IP in use
    uint16_t index_;
    // interface which MetaData IP is associated to
    VmInterface *intf_;
    uint32_t intf_label_;
    IpAddress service_ip_;
    IpAddress destination_ip_;
    bool active_;
    MetaDataIpType type_;
    DISALLOW_COPY_AND_ASSIGN(MetaDataIp);
};

class MetaDataIpAllocator {
public:
    MetaDataIpAllocator(Agent *agent, uint16_t start, uint16_t end);
    ~MetaDataIpAllocator();

    MetaDataIp *FindIndex(uint16_t id);

private:
    friend class MetaDataIp;

    uint16_t AllocateIndex(MetaDataIp *ip);
    void  AllocateIndex(MetaDataIp *ipi, uint16_t index);
    void ReleaseIndex(MetaDataIp *ip);
    void AddFabricRoute(MetaDataIp *ip);
    void DelFabricRoute(MetaDataIp *ip);

    Agent *agent_;
    IndexVector<MetaDataIp *> index_table_;
    uint16_t start_;
    uint16_t end_;
    DISALLOW_COPY_AND_ASSIGN(MetaDataIpAllocator);
};

#endif  // SRC_VNSW_AGENT_OPER_METADATA_IP_H_
