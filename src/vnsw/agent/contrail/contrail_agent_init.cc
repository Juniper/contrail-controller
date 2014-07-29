/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include <pugixml/pugixml.hpp>

#include <db/db.h>
#include <base/logging.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <cmn/agent_param.h>

#include <cfg/cfg_init.h>
#include <cfg/discovery_agent.h>

#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <uve/agent_uve.h>
#include <kstate/kstate.h>
#include <pkt/proto_handler.h>
#include <pkt/agent_stats.h>
#include <diag/diag.h>
#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

#include "contrail_agent_init.h"

ContrailAgentInit::ContrailAgentInit() :
    agent_(NULL), params_(NULL), trigger_() {
}

ContrailAgentInit::~ContrailAgentInit() {
    trigger_->Reset();
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
// Initialization for VMWare specific interfaces
void ContrailAgentInit::InitVmwareInterface() {
    if (!params_->isVmwareMode())
        return;

    PhysicalInterface::Create(agent_->interface_table(),
                              params_->vmware_physical_port(),
                              agent_->fabric_vrf_name(), true);
}

void ContrailAgentInit::InitLogging() {
    Sandesh::SetLoggingParams(params_->log_local(),
                              params_->log_category(),
                              params_->log_level());
}

// Connect to collector specified in config, if discovery server is not set
void ContrailAgentInit::InitCollector() {
    agent_->InitCollector();
}

// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void ContrailAgentInit::CreateModules() {
    agent_->set_cfg(new AgentConfig(agent_));
    agent_->set_stats(new AgentStats(agent_));
    agent_->set_oper_db(new OperDB(agent_));
    agent_->set_uve(AgentObjectFactory::Create<AgentUve>(
                    agent_, AgentUve::kBandwidthInterval));
    agent_->set_ksync(AgentObjectFactory::Create<KSync>(agent_));

    pkt_.reset(new PktModule(agent_));
    agent_->set_pkt(pkt_.get());

    services_.reset(new ServicesModule(agent_,
                                       params_->metadata_shared_secret()));
    agent_->set_services(services_.get());

    agent_->set_vgw(new VirtualGateway(agent_));

    agent_->set_controller(new VNController(agent_));
}

void ContrailAgentInit::CreateDBTables() {
    agent_->cfg()->CreateDBTables(agent_->db());
    agent_->oper_db()->CreateDBTables(agent_->db());
}

void ContrailAgentInit::RegisterDBClients() {
    agent_->cfg()->RegisterDBClients(agent_->db());
    agent_->oper_db()->RegisterDBClients();
    agent_->uve()->RegisterDBClients();
    agent_->ksync()->RegisterDBClients(agent_->db());
    agent_->vgw()->RegisterDBClients();
}

void ContrailAgentInit::InitPeers() {
    agent_->InitPeers();
}

void ContrailAgentInit::InitModules() {
    agent_->cfg()->Init();
    agent_->oper_db()->Init();
    agent_->pkt()->Init(true);
    agent_->services()->Init(true);
    agent_->ksync()->Init(true);
    agent_->uve()->Init();
}

void ContrailAgentInit::CreateVrf() {
    // Create the default VRF
    VrfTable *vrf_table = agent_->vrf_table();

    if (agent_->isXenMode()) {
        vrf_table->CreateStaticVrf(agent_->linklocal_vrf_name());
    }
    vrf_table->CreateStaticVrf(agent_->fabric_vrf_name());

    VrfEntry *vrf = vrf_table->FindVrfFromName(agent_->fabric_vrf_name());
    assert(vrf);

    // Default VRF created; create nexthops
    agent_->set_fabric_inet4_unicast_table(vrf->GetInet4UnicastRouteTable());
    agent_->set_fabric_inet4_multicast_table
        (vrf->GetInet4MulticastRouteTable());
    agent_->set_fabric_l2_unicast_table(vrf->GetLayer2RouteTable());

    // Create VRF for VGw
    agent_->vgw()->CreateVrf();
}

void ContrailAgentInit::CreateNextHops() {
    DiscardNH::Create();
    ResolveNH::Create();

    DiscardNHKey key;
    NextHop *nh = static_cast<NextHop *>
                (agent_->nexthop_table()->FindActiveEntry(&key));
    agent_->nexthop_table()->set_discard_nh(nh);
}

void ContrailAgentInit::CreateInterfaces() {
    InterfaceTable *table = agent_->interface_table();

    PhysicalInterface::Create(table, params_->eth_port(),
                              agent_->fabric_vrf_name(), false);
    InetInterface::Create(table, params_->vhost_name(), InetInterface::VHOST,
                          agent_->fabric_vrf_name(), params_->vhost_addr(),
                          params_->vhost_plen(), params_->vhost_gw(),
                          params_->eth_port(), agent_->fabric_vrf_name());
    agent_->InitXenLinkLocalIntf();
    InitVmwareInterface();

    // Set VHOST interface
    InetInterfaceKey key(agent_->vhost_interface_name());
    agent_->set_vhost_interface
        (static_cast<Interface *>(table->FindActiveEntry(&key)));
    assert(agent_->vhost_interface());

    // Validate physical interface
    PhysicalInterfaceKey physical_key(agent_->fabric_interface_name());
    assert(table->FindActiveEntry(&physical_key));

    agent_->set_router_id(params_->vhost_addr());
    agent_->set_vhost_prefix_len(params_->vhost_plen());
    agent_->set_vhost_default_gateway(params_->vhost_gw());
    agent_->pkt()->CreateInterfaces();
    agent_->vgw()->CreateInterfaces();
}

void ContrailAgentInit::InitDiscovery() {
    agent_->cfg()->InitDiscovery();
}

void ContrailAgentInit::InitDone() {
    //Open up mirror socket
    agent_->mirror_table()->MirrorSockInit();

    agent_->services()->ConfigInit();
    // Diag module needs PktModule
    agent_->set_diag_table(new DiagTable(agent_));
    //Update mac address of vhost interface with
    //that of ethernet interface
    agent_->ksync()->UpdateVhostMac();
    agent_->ksync()->VnswInterfaceListenerInit();

    if (agent_->router_id_configured()) {
        RouterIdDepInit(agent_);
    } else {
        LOG(DEBUG, 
            "Router ID Dependent modules (Nova & BGP) not initialized");
    }

    agent_->cfg()->InitDone();
    agent_->pkt()->InitDone();
}

// Start init sequence
bool ContrailAgentInit::Run() {
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

    agent_->set_init_done(true);
    return true;
}

void ContrailAgentInit::Init(AgentParam *param, Agent *agent,
                     const boost::program_options::variables_map &var_map) {
    params_ = param;
    agent_ = agent;
}

// Trigger inititlization in context of DBTable
void ContrailAgentInit::Start() {
    Module::type module = Module::VROUTER_AGENT;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;
    LoggingInit(params_->log_file(), params_->log_files_count(),
                params_->log_file_size(), params_->use_syslog(),
                params_->syslog_facility(), module_name);

    params_->LogConfig();
    params_->Validate();

    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset(new TaskTrigger(boost::bind(&ContrailAgentInit::Run, this), 
                                   task_id, 0));
    trigger_->Set();
    return;
}
