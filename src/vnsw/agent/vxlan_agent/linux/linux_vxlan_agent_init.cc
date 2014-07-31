/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>

#include <vnc_cfg_types.h>
#include <bgp_schema_types.h>
#include <agent_types.h>

#include <cmn/agent_param.h>
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

#include <vxlan_agent/ksync_vxlan.h>
#include <vxlan_agent/ksync_vxlan_bridge.h>
#include <vxlan_agent/ksync_vxlan_route.h>
#include <vxlan_agent/ksync_vxlan_port.h>

#include "linux_vxlan.h"
#include "linux_bridge.h"
#include "linux_port.h"
#include "linux_fdb.h"

#include "linux_vxlan_agent_init.h"

LinuxVxlanAgentInit::LinuxVxlanAgentInit() 
    : ksync_vxlan_(NULL) {
}

LinuxVxlanAgentInit::~LinuxVxlanAgentInit() {
    ksync_vxlan_.reset(NULL);
}

void LinuxVxlanAgentInit::ProcessOptions
    (const std::string &config_file, const std::string &program_name,
     const boost::program_options::variables_map &var_map) {

    AgentInit::ProcessOptions(config_file, program_name, var_map);
}

int LinuxVxlanAgentInit::Start() {
    return AgentInit::Start();
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
void LinuxVxlanAgentInit::FactoryInit() {
}

void LinuxVxlanAgentInit::CreateModules() {
    ksync_vxlan_.reset(new KSyncLinuxVxlan(agent()));
}

void LinuxVxlanAgentInit::RegisterDBClients() {
    ksync_vxlan_->RegisterDBClients(agent()->db());
}

void LinuxVxlanAgentInit::InitModules() {
    ksync_vxlan_->Init();
}

void LinuxVxlanAgentInit::ConnectToController() {
    agent()->controller()->Connect();
}

/****************************************************************************
 * Shutdown routines
 ****************************************************************************/
void LinuxVxlanAgentInit::KSyncShutdown() {
    ksync_vxlan_->Shutdown();
}

void LinuxVxlanAgentInit::WaitForIdle() {
    sleep(5);
}
