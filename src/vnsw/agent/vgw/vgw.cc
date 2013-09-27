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
#include <oper/inet4_ucroute.h>
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

    Inet4UcRouteTable *rt_table;
    rt_table = Agent::GetInstance()->GetVrfTable()->GetInet4UcRouteTable
        (cfg->GetVrf());
    rt_table->AddVHostRecvRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                cfg->GetVrf(), cfg->GetInterface(),
                                cfg->GetAddr(), 32, cfg->GetVrf(), false);
    rt_table->AddVHostRecvRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                cfg->GetVrf(), cfg->GetInterface(), 
                                Ip4Address(0), 0, cfg->GetVrf(), false);
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
