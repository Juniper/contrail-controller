/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/route_common.h>
#include <oper/ecmp_load_balance.h>
#include <oper/interface_common.h>
#include <oper/vm_interface.h>
#include <oper/inet_unicast_route.h>
#include <oper/metadata_ip.h>

const IpAddress MetaDataIp::kDefaultIp;

MetaDataIp::MetaDataIp(MetaDataIpAllocator *allocator, VmInterface *intf,
                       MetaDataIpType type) :
    allocator_(allocator), index_(-1), intf_(intf),
    intf_label_(MplsTable::kInvalidLabel), service_ip_(), destination_ip_(),
    active_(false), type_(type) {
    index_ = allocator_->AllocateIndex(this);
    intf->InsertMetaDataIpInfo(this);
}

MetaDataIp::MetaDataIp(MetaDataIpAllocator *allocator, VmInterface *intf,
                       uint16_t index) :
    allocator_(allocator), index_(index), intf_(intf),
    intf_label_(MplsTable::kInvalidLabel), service_ip_(), destination_ip_(),
    active_(false), type_(LINKLOCAL) {
    allocator_->AllocateIndex(this, index_);
    intf_->InsertMetaDataIpInfo(this);
}

MetaDataIp::~MetaDataIp() {
    intf_->DeleteMetaDataIpInfo(this);
    set_active(false);
    allocator_->ReleaseIndex(this);
}

Ip4Address MetaDataIp::GetLinkLocalIp() const {
    uint32_t ip = METADATA_IP_ADDR & 0xFFFF0000;
    ip += (((uint32_t)index_) & 0xFFFF);
    return Ip4Address(ip);
}

IpAddress MetaDataIp::service_ip() const {
    // check if explicit configuration of service ip is present for
    // this metadata ip
    if (service_ip_ == kDefaultIp) {
        IpAddress service_ip;
        if (type_ == HEALTH_CHECK) {
            // for metadata IP type Health Check first verify
            // if service health check ip is available on interface
            service_ip =  intf_->service_health_check_ip();
            if (service_ip != kDefaultIp) {
                return service_ip;
            }
        }
        // check if service ip on the primary ip addr of interface
        // is available
        service_ip = intf_->GetServiceIp(intf_->primary_ip_addr());
        if (service_ip != kDefaultIp) {
            return service_ip;
        }
        // if service IP is not available fallback to MetaData IP
        return Ip4Address(METADATA_IP_ADDR);
    }
    return service_ip_;
}

IpAddress MetaDataIp::destination_ip() const {
    if (destination_ip_.to_v4() == kDefaultIp) {
        return intf_->primary_ip_addr();
    }
    return destination_ip_;
}

void MetaDataIp::set_destination_ip(const IpAddress &dst_ip) {
    destination_ip_ = dst_ip;
}

void MetaDataIp::set_active(bool active) {
    if (active_ != active) {
        active_ = active;
        UpdateRoute();
    }
}

void MetaDataIp::UpdateInterfaceCb() {
    if (intf_label_ != intf_->label()) {
        intf_label_ = intf_->label();
        UpdateRoute();
    }
}

void MetaDataIp::UpdateRoute() {
    if (active_ && intf_->label() != MplsTable::kInvalidLabel) {
        intf_label_ = intf_->label();
        allocator_->AddFabricRoute(this);
    } else {
        allocator_->DelFabricRoute(this);
        intf_label_ = MplsTable::kInvalidLabel;
    }
}

MetaDataIpAllocator::MetaDataIpAllocator(Agent *agent, uint16_t start,
                                         uint16_t end) :
    agent_(agent), index_table_(), start_(start), end_(end) {
}

MetaDataIpAllocator::~MetaDataIpAllocator() {
}

MetaDataIp *MetaDataIpAllocator::FindIndex(uint16_t id) {
    assert(id <= end_ && id >= start_);
    uint16_t index = end_ - id;
    return index_table_.At(index);
}

uint16_t MetaDataIpAllocator::AllocateIndex(MetaDataIp *ip) {
    uint16_t index = index_table_.Insert(ip);
    assert(index <= end_ && (end_ - index) >= start_);
    return (end_ - index);
}

void MetaDataIpAllocator::AllocateIndex(MetaDataIp *ip, uint16_t id) {
    assert(id <= end_ && id >= start_);
    uint16_t index = end_ - id;
    assert(index == index_table_.InsertAtIndex(index, ip));
}

void MetaDataIpAllocator::ReleaseIndex(MetaDataIp *ip) {
    uint16_t index = end_ - ip->index_;
    index_table_.Remove(index);
}

void MetaDataIpAllocator::AddFabricRoute(MetaDataIp *ip) {
    PathPreference path_preference;
    EcmpLoadBalance ecmp_load_balance;
    ip->intf_->SetPathPreference(&path_preference, false, Ip4Address(0));

    VnListType vn_list;
    vn_list.insert(ip->intf_->vn()->GetName());
    InetUnicastAgentRouteTable::AddLocalVmRoute
        (agent_->link_local_peer(), agent_->fabric_vrf_name(),
         ip->GetLinkLocalIp(), 32, ip->intf_->GetUuid(),
         vn_list, ip->intf_->label(), SecurityGroupList(),
         CommunityList(), true, path_preference, Ip4Address(0),
         ecmp_load_balance, false, false);
}

void MetaDataIpAllocator::DelFabricRoute(MetaDataIp *ip) {
    InetUnicastAgentRouteTable::Delete(agent_->link_local_peer(),
                                       agent_->fabric_vrf_name(),
                                       ip->GetLinkLocalIp(), 32);
}

