/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "services/dhcp_proto.h"

void DhcpProto::Shutdown() {
}

DhcpProto::DhcpProto(Agent *agent, boost::asio::io_service &io,
                     bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::DHCP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_interface_(NULL),
    ip_fabric_interface_index_(-1) {
#if defined(__linux__)
    memset(ip_fabric_interface_mac_, 0, ETH_ALEN);
#elif defined(__FreeBSD__)
    memset(ip_fabric_interface_mac_, 0, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
    iid_ = agent->GetInterfaceTable()->Register(
                  boost::bind(&DhcpProto::ItfNotify, this, _2));
}

DhcpProto::~DhcpProto() {
    agent_->GetInterfaceTable()->Unregister(iid_);
}

ProtoHandler *DhcpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new DhcpHandler(agent(), info, io);
}

void DhcpProto::ItfNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            set_ip_fabric_interface(NULL);
            set_ip_fabric_interface_index(-1);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            if (run_with_vrouter_) {
#if defined(__linux__)
                set_ip_fabric_interface_mac((char *)itf->mac().ether_addr_octet);
            } else {
                char mac[ETH_ALEN];
                memset(mac, 0, ETH_ALEN);
#elif defined(__FreeBSD__)
                set_ip_fabric_interface_mac((char *)itf->mac().octet);
            } else {
                char mac[ETHER_ADDR_LEN];
                memset(mac, 0, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
                set_ip_fabric_interface_mac(mac);
            }
        }
    }
}
