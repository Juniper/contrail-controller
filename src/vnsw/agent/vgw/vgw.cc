/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>

#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <cfg/init_config.h>

#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/agent_route.h>
#include <oper/interface.h>
#include <oper/vrf_assign.h>

#include <vgw/vgw_cfg.h>
#include <vgw/vgw.h>

using namespace std;

VGwTable *VGwTable::singleton_;

VGwTable::VGwTable() : lid_(0), label_(MplsTable::kInvalidLabel) {
}

void VGwTable::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    Interface::Type type = itf->GetType();
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

    VGwConfig *cfg = VGwConfig::GetInstance();
    // Allocate MPLS Label 
    label_ = Agent::GetInstance()->GetMplsTable()->AllocLabel();

    // Create InterfaceNH before MPLS is created
    ReceiveNH::CreateReq(cfg->GetInterface());

    // Create MPLS entry pointing to virtual host interface-nh
    MplsLabel::CreateVirtualHostPortLabelReq(label_, cfg->GetInterface(),
                                             false);

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (VrfTable::GetInstance()->GetRouteTable(cfg->GetVrf(), 
                                  AgentRouteTableAPIS::INET4_UNICAST));

    // Packets received on fabric vrf and destined to IP address in "public" 
    // network reach kernel. Linux kernel will put back the packets on vgw
    // interface. Add route to trap the public addresses to linux kernel
    Ip4Address addr = cfg->GetAddr();
    addr = Ip4Address(addr.to_ulong() & (0xFFFFFFFF << (32 - cfg->GetPlen())));
    rt_table->AddVHostRecvRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                Agent::GetInstance()->GetDefaultVrf(),
                                Agent::GetInstance()->GetVirtualHostInterfaceName(),
                                addr, cfg->GetPlen(), cfg->GetVrf(), false);

    // Add default route in public network. BGP will export this route to
    // other compute nodes
    rt_table->AddVHostInterfaceRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                     cfg->GetVrf(), Ip4Address(0), 0,
                                     cfg->GetInterface(), label_,
                                     cfg->GetVrf());
}

void VGwTable::CreateStaticObjects() {
}


void VGwTable::Init() {
    assert(singleton_ == NULL);
    singleton_ = new VGwTable();

    singleton_->lid_ = Agent::GetInstance()->GetInterfaceTable()->Register
        (boost::bind(&VGwTable::InterfaceNotify, singleton_, _1, _2));

    VGwConfig *cfg = VGwConfig::GetInstance();
    if (cfg == NULL) {
        return;
    }
    Agent::GetInstance()->GetVrfTable()->CreateVrf(cfg->GetVrf());
    VirtualHostInterface::CreateReq(cfg->GetInterface(), cfg->GetVrf(), 
                                    VirtualHostInterface::GATEWAY);
}

void VGwTable::Shutdown() {
    Agent::GetInstance()->GetInterfaceTable()->Unregister(singleton_->lid_);
    delete singleton_;
    singleton_ = NULL;
}
