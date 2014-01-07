/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "services/dhcp_proto.h"

void DhcpProto::Init() {
}

void DhcpProto::Shutdown() {
}

DhcpProto::DhcpProto(Agent *agent, boost::asio::io_service &io,
                     bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::DHCP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_intf_(NULL),
    ip_fabric_intf_index_(-1) {
    memset(ip_fabric_intf_mac_, 0, ETH_ALEN);
    iid_ = agent->GetInterfaceTable()->Register(
                  boost::bind(&DhcpProto::ItfUpdate, this, _2));
}

DhcpProto::~DhcpProto() {
    agent_->GetInterfaceTable()->Unregister(iid_);
}

ProtoHandler *DhcpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new DhcpHandler(agent(), info, io);
}

void DhcpProto::ItfUpdate(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            IPFabricIntf(NULL);
            IPFabricIntfIndex(-1);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            IPFabricIntf(itf);
            IPFabricIntfIndex(itf->id());
            if (run_with_vrouter_) {
                IPFabricIntfMac((char *)itf->mac().ether_addr_octet);
            } else {
                char mac[ETH_ALEN];
                memset(mac, 0, ETH_ALEN);
                IPFabricIntfMac(mac);
            }
        }
    }
}
