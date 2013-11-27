/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>

#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>

#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/agent_route.h>
#include <oper/interface.h>
#include <oper/vrf_assign.h>

#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

using namespace std;

VirtualGateway::VirtualGateway(Agent *agent) : agent_(agent), lid_(0),
    label_(MplsTable::kInvalidLabel) {
    vgw_config_ = agent->params()->vgw_config();
    if (vgw_config_->vrf() == "")
        vgw_config_ = NULL;
}

void VirtualGateway::InterfaceNotify(DBTablePartBase *partition,
                                     DBEntryBase *entry) {
    Interface *interface = static_cast<Interface *>(entry);
    Interface::Type type = interface->GetType();
    if (type != Interface::VHOST)
        return;

    VirtualHostInterface *vhost = static_cast<VirtualHostInterface *>(entry);
    if (vhost->GetSubType() != VirtualHostInterface::GATEWAY)
        return;

    if (entry->IsDeleted()) {
        return;
    }

    if (label_ != MplsTable::kInvalidLabel) {
        return;
    }

    // Allocate MPLS Label 
    label_ = agent_->GetMplsTable()->AllocLabel();

    // Create InterfaceNH before MPLS is created
    InterfaceNH::CreateVirtualHostPort(vgw_config_->interface());

    // Create MPLS entry pointing to virtual host interface-nh
    MplsLabel::CreateVirtualHostPortLabel(label_, vgw_config_->interface(),
                                          false, InterfaceNHFlags::INET4);

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (VrfTable::GetInstance()->GetRouteTable(vgw_config_->vrf(), 
                                  AgentRouteTableAPIS::INET4_UNICAST));

    // Packets received on fabric vrf and destined to IP address in "public" 
    // network reach kernel. Linux kernel will put back the packets on vgw
    // interface. Add route to trap the public addresses to linux kernel
    Ip4Address addr = vgw_config_->ip();
    addr = Ip4Address(addr.to_ulong() & (0xFFFFFFFF <<
                                         (32 - vgw_config_->plen())));
    rt_table->AddVHostRecvRoute(agent_->GetLocalVmPeer(),
                                agent_->GetDefaultVrf(),
                                agent_->GetVirtualHostInterfaceName(),
                                addr, vgw_config_->plen(), vgw_config_->vrf(),
                                false);

    // Add default route in public network. BGP will export this route to
    // other compute nodes
    rt_table->AddVHostInterfaceRoute(agent_->GetLocalVmPeer(),
                                     vgw_config_->vrf(), Ip4Address(0), 0,
                                     vgw_config_->interface(), label_,
                                     vgw_config_->vrf());
}

void VirtualGateway::RegisterDBClients() {
    lid_ = agent_->GetInterfaceTable()->Register
        (boost::bind(&VirtualGateway::InterfaceNotify, this, _1, _2));
}

// Create VRF for "public" virtual-network
void VirtualGateway::CreateVrf() {
    if (vgw_config_ == NULL) {
        return;
    }
    agent_->GetVrfTable()->CreateVrf(vgw_config_->vrf());
}

// Create virtual-gateway interface
void VirtualGateway::CreateInterfaces() {
    if (vgw_config_ == NULL) {
        return;
    }
    VirtualHostInterface::CreateReq(agent_->GetInterfaceTable(),
                                    vgw_config_->interface(),
                                    vgw_config_->vrf(), 
                                    VirtualHostInterface::GATEWAY);
}

void VirtualGateway::Init() {
}

void VirtualGateway::Shutdown() {
    if (vgw_config_ == NULL)
        return;

    agent_->GetInterfaceTable()->Unregister(lid_);

    // Delete routes
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (VrfTable::GetInstance()->GetRouteTable(vgw_config_->vrf(), 
                                  AgentRouteTableAPIS::INET4_UNICAST));
    if (rt_table == NULL)
        return;

    Ip4Address addr = vgw_config_->ip();
    addr = Ip4Address(addr.to_ulong() & (0xFFFFFFFF <<
                                         (32 - vgw_config_->plen())));
    rt_table->DeleteReq(agent_->GetLocalVmPeer(),
                        agent_->GetDefaultVrf(),
                        addr, vgw_config_->plen());

    rt_table->DeleteReq(agent_->GetLocalVmPeer(),
                        vgw_config_->vrf(), Ip4Address(0), 0);

    // Delete NH
    InterfaceNH::DeleteVirtualHostPortReq(vgw_config_->interface());

    // Delete MPLS Label
    MplsLabel::DeleteReq(label_);

    // Delete Interface
    VirtualHostInterface::DeleteReq(agent_->GetInterfaceTable(),
                                    vgw_config_->interface());

    // Delete VRF for "public" virtual-network
    agent_->GetVrfTable()->DeleteVrf(vgw_config_->vrf());
}
