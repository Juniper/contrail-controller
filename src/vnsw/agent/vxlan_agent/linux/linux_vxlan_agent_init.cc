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

/****************************************************************************
 * Cleanup routines on shutdown
****************************************************************************/
void LinuxVxlanAgentInit::Shutdown() {
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
void LinuxVxlanAgentInit::InitLogging() {
    Sandesh::SetLoggingParams(params_->log_local(),
                              params_->log_category(),
                              params_->log_level());
}

// Connect to collector specified in config, if discovery server is not set
void LinuxVxlanAgentInit::InitCollector() {
    agent_->InitCollector();
}

// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void LinuxVxlanAgentInit::CreateModules() {
    agent_->set_cfg(new AgentConfig(agent_));
    agent_->set_oper_db(new OperDB(agent_));
    agent_->set_controller(new VNController(agent_));
    ksync_vxlan_.reset(new KSyncLinuxVxlan(agent_));
}

void LinuxVxlanAgentInit::CreateDBTables() {
    agent_->cfg()->CreateDBTables(agent_->db());
    agent_->oper_db()->CreateDBTables(agent_->db());
}

void LinuxVxlanAgentInit::RegisterDBClients() {
    agent_->cfg()->RegisterDBClients(agent_->db());
    ksync_vxlan_->RegisterDBClients(agent_->db());
}

void LinuxVxlanAgentInit::InitPeers() {
    agent_->InitPeers();
}

void LinuxVxlanAgentInit::InitModules() {
    agent_->cfg()->Init();
    agent_->oper_db()->Init();
    ksync_vxlan_->Init();
}

void LinuxVxlanAgentInit::CreateVrf() {
    // Create the default VRF
    VrfTable *vrf_table = agent_->vrf_table();

    vrf_table->CreateStaticVrf(agent_->fabric_vrf_name());
    VrfEntry *vrf = vrf_table->FindVrfFromName(agent_->fabric_vrf_name());
    assert(vrf);

    // Default VRF created; create nexthops

    agent_->set_fabric_inet4_unicast_table(vrf->GetInet4UnicastRouteTable());
    agent_->set_fabric_inet4_multicast_table
        (vrf->GetInet4MulticastRouteTable());
    agent_->set_fabric_l2_unicast_table(vrf->GetLayer2RouteTable());
}

void LinuxVxlanAgentInit::CreateNextHops() {
    DiscardNH::Create();
    ResolveNH::Create();

    DiscardNHKey key;
    NextHop *nh = static_cast<NextHop *>
                (agent_->nexthop_table()->FindActiveEntry(&key));
    agent_->nexthop_table()->set_discard_nh(nh);
}

void LinuxVxlanAgentInit::CreateInterfaces() {
}

void LinuxVxlanAgentInit::InitDiscovery() {
    agent_->cfg()->InitDiscovery();
}

void LinuxVxlanAgentInit::InitDone() {
    RouterIdDepInit(agent_);
    agent_->cfg()->InitDone();
}

void LinuxVxlanAgentInit::InitXenLinkLocalIntf() {
    assert(0);
}

// Start init sequence
bool LinuxVxlanAgentInit::Run() {
    InitLogging();
    InitCollector();
    InitPeers();
    CreateModules();
    CreateDBTables();
    RegisterDBClients();
    InitModules();
    CreateVrf();
    CreateNextHops();
    InitDiscovery();
    CreateInterfaces();
    InitDone();

    init_done_ = true;
    return true;
}

void LinuxVxlanAgentInit::Init(AgentParam *param, Agent *agent,
                     const boost::program_options::variables_map &var_map) {
    params_ = param;
    agent_ = agent;
}

// Trigger inititlization in context of DBTable
void LinuxVxlanAgentInit::Start() {
    Module::type module = Module::VROUTER_AGENT;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;
    LoggingInit(params_->log_file(), params_->log_files_count(),
                params_->log_file_size(), params_->use_syslog(),
                params_->syslog_facility(), module_name);

    params_->LogConfig();
    params_->Validate();

    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset(new TaskTrigger(boost::bind(&LinuxVxlanAgentInit::Run, this),
                                   task_id, 0));
    trigger_->Set();
    return;
}
