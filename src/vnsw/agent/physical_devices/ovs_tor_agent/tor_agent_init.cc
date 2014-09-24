/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>

#include <vnc_cfg_types.h>
#include <bgp_schema_types.h>
#include <agent_types.h>

#include <init/agent_param.h>
#include <cmn/agent_db.h>

#include <oper/operdb_init.h>
#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/multicast.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/layer2_route.h>

#include <cfg/cfg_init.h>
#include <controller/controller_init.h>
#include <physical_devices/tables/device_manager.h>

#include <physical_devices/ovs_tor_agent/tor_agent_init.h>
#include <physical_devices/ovs_tor_agent/tor_agent_param.h>

#include <string>

using std::string;
using std::cout;

TorAgentInit::TorAgentInit()  {
}

TorAgentInit::~TorAgentInit() {
}

void TorAgentInit::ProcessOptions
    (const std::string &config_file, const std::string &program_name) {
    AgentInit::ProcessOptions(config_file, program_name);
}

int TorAgentInit::Start() {
    return AgentInit::Start();
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
string TorAgentInit::InstanceId() {
    TorAgentParam *param = dynamic_cast<TorAgentParam *>(agent_param());
    return param->tor_id();
}

string TorAgentInit::ModuleName() {
    return "TorAgent-" + InstanceId();
}

void TorAgentInit::FactoryInit() {
}

void TorAgentInit::CreateModules() {
    device_manager_.reset(new PhysicalDeviceManager(agent()));
    agent()->set_device_manager(device_manager_.get());
}

void TorAgentInit::CreateDBTables() {
    device_manager_->CreateDBTables(agent()->db());
}

void TorAgentInit::RegisterDBClients() {
    device_manager_->RegisterDBClients();
}

void TorAgentInit::InitModules() {
    device_manager_->Init();
}

void TorAgentInit::ConnectToController() {
    agent()->controller()->Connect();
}

/****************************************************************************
 * Shutdown routines
 ****************************************************************************/
void TorAgentInit::WaitForIdle() {
    sleep(5);
}
