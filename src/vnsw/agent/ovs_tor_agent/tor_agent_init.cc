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
#include <oper/bridge_route.h>
#include <uve/agent_uve.h>

#include <cfg/cfg_init.h>
#include <controller/controller_init.h>

#include <ovs_tor_agent/tor_agent_init.h>
#include <ovs_tor_agent/tor_agent_param.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_client.h>

#include <string>

using std::string;
using std::cout;
using OVSDB::OvsdbClient;

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

int TorAgentInit::ModuleType() {
    return Module::TOR_AGENT;
}

void TorAgentInit::FactoryInit() {
}

void TorAgentInit::CreatePeers() {
    ovs_peer_manager_.reset(new OvsPeerManager(agent()));
}

void TorAgentInit::CreateModules() {
    ovsdb_client_.reset(OvsdbClient::Allocate(agent(),
                static_cast<TorAgentParam *>(agent_param()),
                ovs_peer_manager()));
    agent()->set_ovsdb_client(ovsdb_client_.get());
    uve_.reset(new AgentUve(agent(), AgentUveBase::kBandwidthInterval,
                            AgentUveBase::kDefaultInterval,
                            AgentUveBase::kIncrementalInterval));
    agent()->set_uve(uve_.get());
}

void TorAgentInit::CreateDBTables() {
}

void TorAgentInit::RegisterDBClients() {
    ovsdb_client_->RegisterClients();
    uve_->RegisterDBClients();
}

void TorAgentInit::InitModules() {
    uve_->Init();
}

void TorAgentInit::ConnectToController() {
    agent()->controller()->Connect();
}

/****************************************************************************
 * Shutdown routines
 ****************************************************************************/
void TorAgentInit::UveShutdown() {
    uve_->Shutdown();
}

void TorAgentInit::WaitForIdle() {
    sleep(5);
}

/****************************************************************************
 * Access routines
 ****************************************************************************/
OvsPeerManager *TorAgentInit::ovs_peer_manager() const {
    return ovs_peer_manager_.get();
}
