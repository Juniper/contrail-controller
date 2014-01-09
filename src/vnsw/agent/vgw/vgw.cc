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
#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/vrf_assign.h>

#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

using namespace std;

VirtualGateway::VirtualGateway(Agent *agent) : agent_(agent) {
    vgw_config_ = agent->params()->vgw_config();
    if (vgw_config_->vrf() == "")
        vgw_config_ = NULL;
}

void VirtualGateway::RegisterDBClients() {
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
    InetInterface::CreateReq(agent_->GetInterfaceTable(),
                             vgw_config_->interface(),
                             InetInterface::SIMPLE_GATEWAY,
                             vgw_config_->vrf(), vgw_config_->ip(),
                             vgw_config_->plen(), Ip4Address(0), "");
}

void VirtualGateway::Init() {
}

void VirtualGateway::Shutdown() {
    if (vgw_config_ == NULL)
        return;

    // Delete Interface
    InetInterface::DeleteReq(agent_->GetInterfaceTable(),
                             vgw_config_->interface());

    // Delete VRF for "public" virtual-network
    agent_->GetVrfTable()->DeleteVrf(vgw_config_->vrf());
}
